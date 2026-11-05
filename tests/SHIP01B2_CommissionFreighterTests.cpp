#include "galaxy/Persistence.hpp"
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do {if(!(x))throw std::runtime_error(std::string("SHIP-01B.2: ")+#x+" line "+std::to_string(__LINE__));}while(false)

static void fabricate(PersistentWorld& w,EntityId source,EntityId rock,
                      std::uint64_t& serial,std::int64_t batches) {
    auto work=[&](Action action,std::int64_t quantity,EntityId target=0){
        Command c{};c.id=++serial;c.sequence=c.id;
        c.execute_tick=w.world().tick()+1;c.issuer=7;c.actor=source;
        c.target=target;c.action=action;c.amount=quantity;
        CHECK(w.submit(c));w.step();
        CHECK(w.world().events().back().result==Outcome::Applied);
    };
    for(std::int64_t i=0;i<batches;++i){
        work(Action::Mine,4,rock);
        work(Action::RefineOre,4);
        work(Action::FabricateModule,2);
    }
}
int main() {
    const auto db=fs::temp_directory_path()/
        ("single-galaxy-ship01b2-"+std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count())+".sqlite");
    EntityId origin=0,hull=0;
    std::array<std::uint64_t,2> builders{},crew{};
    std::uint64_t serial=9000;
    std::int64_t baseline=0;
    std::string checkpoint;
    try{
        {
            Simulation genesis(881);
            origin=genesis.add_colony_ship(7,{0,0},100,6);
            const auto rock=genesis.add_asteroid({2000,0},100);
            genesis.provision_colony_genesis(origin,2000,2000,2000,2000,2000);
            genesis.enable_individual_workforce();
            genesis.ensure_physical_celestial_systems();
            genesis.ensure_ship_frame_registry();
            std::vector<std::uint64_t> ids;
            for(const auto& [id,n]:genesis.npc_records())
                if(n.home_ship==origin)ids.push_back(id);
            CHECK(ids.size()==6);
            builders={ids[0],ids[1]};
            crew={ids[2],ids[3]};
            baseline=genesis.total_ore();
            PersistentWorld w(db.string(),std::move(genesis));
            fabricate(w,origin,rock,serial,2);
            CHECK(w.start_shipyard_construction(7,origin,91001,builders));
            w.steps(8);
            fabricate(w,origin,rock,serial,3);
            CHECK(w.queue_shipyard_freighter(7,91001,92001));
            w.steps(12);
            const auto& manufacturing=w.world().freighter_build_orders().at(92001);
            CHECK(manufacturing.finished());
            hull=manufacturing.launched_ship;
            CHECK(hull!=origin && w.world().find(hull));
            CHECK(!manufacturing.commissioned());
            CHECK(!w.world().colony_ship(hull));
            CHECK(!w.world().life_support(hull));
            CHECK(!w.commission_shipyard_freighter(8,92001,crew,20));
            CHECK(!w.commission_shipyard_freighter(7,92001,{crew[0],crew[0]},20));
            CHECK(!w.commission_shipyard_freighter(7,92001,builders,20));
            CHECK(!w.commission_shipyard_freighter(7,92001,crew,20));
            fabricate(w,origin,rock,serial,1); // actual scanner inventory
            const auto source_modules=w.world().colony_ship(origin)->modules;
            CHECK(source_modules==2);
            const auto source_crew=w.world().colony_ship(origin)->crew;
            const auto living=w.world().npc_records().size();
            const auto source_food=w.world().life_support(origin)->food_kg;
            const auto source_water=w.world().life_support(origin)->water_kg;
            const auto source_oxygen=w.world().life_support(origin)->oxygen_kg;
            const auto previous=w.world().canonical_state();
            const auto generation=w.generation();
            bool failure=false;
            try{(void)w.commission_shipyard_freighter(7,92001,crew,20,
                FailPoint::AfterSnapshotWrite);}
            catch(const std::exception&){failure=true;}
            CHECK(failure && w.generation()==generation);
            CHECK(w.world().canonical_state()==previous);
            CHECK(w.world().colony_ship(hull)==nullptr);
            CHECK(w.world().total_ore()==baseline);
            CHECK(w.commission_shipyard_freighter(7,92001,crew,20));
            CHECK(!w.commission_shipyard_freighter(7,92001,crew,20));
            CHECK(w.world().canonical_state().rfind("galaxy-kernel-v92|",0)==0);
            CHECK(w.world().freighter_build_orders().at(92001).commissioned());
            CHECK(w.world().freighter_build_orders().at(92001).commissioning_crew==crew);
            CHECK(w.world().freighter_build_orders().at(92001).provisions_kg_each==20);
            CHECK(w.world().colony_ship(origin)->modules==source_modules-1);
            CHECK(w.world().colony_ship(origin)->crew==source_crew-2);
            CHECK(w.world().colony_ship(hull)->crew==2);
            CHECK(w.world().find(hull)->fitting.exploration_scanner());
            CHECK(w.world().life_support(origin)->food_kg==source_food-20);
            CHECK(w.world().life_support(origin)->water_kg==source_water-20);
            CHECK(w.world().life_support(origin)->oxygen_kg==source_oxygen-20);
            CHECK(w.world().life_support(hull)->food_kg==20);
            CHECK(w.world().life_support(hull)->water_kg==20);
            CHECK(w.world().life_support(hull)->oxygen_kg==20);
            CHECK(w.world().npc_records().size()==living);
            for(const auto id:crew){
                const auto& n=w.world().npc_records().at(id);
                CHECK(n.home_ship==hull && n.location==hull);
                CHECK(n.assigned_site==hull && n.legal_residence().id==hull);
            }
            CHECK(w.world().npc_records().at(builders[0]).home_ship==origin);
            CHECK(w.world().npc_records().at(builders[1]).home_ship==origin);
            CHECK(w.world().total_ore()==baseline);
            checkpoint=w.world().canonical_state();
            CHECK(Simulation::from_snapshot(checkpoint).canonical_state()==checkpoint);
            // Tamper with the durable commissioning stock transfer witness.
            auto altered=checkpoint;
            const auto at=altered.find("freighterbuild|92001|");
            CHECK(at!=std::string::npos);
            const auto end=altered.find('\n',at);
            CHECK(end!=std::string::npos);
            const auto receipt=altered.substr(at,end-at);
            const auto bar=receipt.rfind('|');
            CHECK(bar!=std::string::npos);
            altered.replace(at+bar+1,end-(at+bar+1),"0");
            bool rejected=false;
            try{(void)Simulation::from_snapshot(altered);}
            catch(const std::exception&){rejected=true;}
            CHECK(rejected);
        }
        {
            PersistentWorld w(db.string(),Simulation(881));
            CHECK(w.world().canonical_state()==checkpoint);
            CHECK(w.world().total_ore()==baseline);
            CHECK(w.world().colony_ship(hull)->crew==2);
            CHECK(w.world().find(hull)->fitting.exploration_scanner());
            Command flight{};
            flight.id=990000;flight.sequence=990000;
            flight.execute_tick=w.world().tick()+1;
            flight.issuer=7;flight.actor=hull;
            flight.action=Action::Thrust;flight.thrust_x=1;
            CHECK(w.submit(flight)); // real owner command, no phantom pilot
            w.step();
            CHECK(w.world().events().back().result==Outcome::Applied);
            CHECK(w.world().total_ore()==baseline);
        }
        fs::remove(db);
        std::cout<<"PASS SHIP-01B.2: physical named crew commissioning, "
            "paid scanner and provisions, native flight and SQLite recovery\n";
    }catch(...){fs::remove(db);throw;}
}
