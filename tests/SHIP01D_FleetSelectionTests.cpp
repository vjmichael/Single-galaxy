#include "galaxy/FleetSelection.hpp"
#include "galaxy/ClientContract.hpp"
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
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("SHIP-01D: ")+#x+" line "+std::to_string(__LINE__));}while(false)

static void fabricate(PersistentWorld& w,EntityId source,EntityId rock,
                      std::uint64_t& serial,int batches) {
    auto apply=[&](Action action,std::int64_t n,EntityId target=0){
        Command c{};c.id=++serial;c.sequence=c.id;
        c.execute_tick=w.world().tick()+1;
        c.issuer=7;c.actor=source;c.action=action;
        c.amount=n;c.target=target;
        CHECK(w.submit(c));w.step();
        CHECK(w.world().events().back().result==Outcome::Applied);
    };
    for(int i=0;i<batches;++i) {
        apply(Action::Mine,4,rock);
        apply(Action::RefineOre,4);
        apply(Action::FabricateModule,2);
    }
}

static bool visible(const ViewFrame& frame,EntityId id) {
    for(const auto& change:frame.changes)
        if(change.kind==ChangeKind::Upsert && change.entity.id==id)
            return true;
    return false;
}

int main() {
    const auto db=fs::temp_directory_path()/
        ("sg-ship01d-"+std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count())+".sqlite");
    EntityId origin=0,hull=0,foreign=0;
    std::uint64_t body=0;
    std::string durable;
    try{
        {
            Simulation seed(881);
            origin=seed.add_colony_ship(7,{0,0},100,6);
            const auto rock=seed.add_asteroid({2000,0},100);
            foreign=seed.add_colony_ship(8,{0,0},100,2);
            seed.provision_colony_genesis(origin,2000,2000,2000,2000,2000);
            seed.provision_colony_genesis(foreign,2000,2000,2000,2000,2000);
            seed.enable_individual_workforce();
            seed.provision_starter_exploration_scanner(origin);
            seed.ensure_physical_celestial_systems();
            seed.ensure_ship_frame_registry();
            for(const auto& [system,objects]:seed.physical_celestial_systems()) {
                (void)system;
                for(const auto& obj:objects)
                    if(!body && (obj.kind==CelestialKind::Planet ||
                                 obj.kind==CelestialKind::Moon))body=obj.id;
            }
            CHECK(body!=0);
            std::vector<std::uint64_t> named;
            for(const auto& [id,n]:seed.npc_records())
                if(n.home_ship==origin && n.location==origin)named.push_back(id);
            CHECK(named.size()==6);
            const std::array<std::uint64_t,2> builders{{named[0],named[1]}};
            const std::array<std::uint64_t,2> pilots{{named[2],named[3]}};
            const auto total_people=seed.npc_records().size();
            const auto conserved=seed.total_ore();
            PersistentWorld w(db.string(),std::move(seed));
            FleetSelection fleet(7,origin);
            CHECK(fleet.home()==origin&&fleet.piloted()==origin);
            CHECK(fleet.is_ready(w.world(),origin));
            CHECK(!fleet.select(w.world(),foreign));
            CHECK(!fleet.select(w.world(),rock));
            CHECK(!fleet.select(w.world(),99999999));
            CHECK(fleet.piloted()==origin);
            CHECK(fleet.list(w.world()).size()==1);

            std::uint64_t serial=12000;
            fabricate(w,origin,rock,serial,2);
            CHECK(w.start_shipyard_construction(7,origin,91001,builders));
            w.steps(8);
            fabricate(w,origin,rock,serial,3);
            CHECK(w.queue_shipyard_freighter(7,91001,92001));
            w.steps(12);
            hull=w.world().freighter_build_orders().at(92001).launched_ship;
            CHECK(hull&&hull!=origin);
            auto ships=fleet.list(w.world());
            CHECK(ships.size()==2);
            CHECK(ships[0].home && ships[0].ship==origin);
            CHECK(ships[1].ship==hull && ships[1].manufacture_order==92001);
            CHECK(!ships[1].ready && ships[1].crew==0);
            CHECK(!fleet.select(w.world(),hull)); // cannot steer uncrewed hull
            CHECK(!fleet.select(w.world(),foreign));

            fabricate(w,origin,rock,serial,1);
            CHECK(w.commission_shipyard_freighter(7,92001,pilots,200));
            const auto before_select=w.world().canonical_state();
            const auto generation=w.generation();
            CHECK(fleet.select(w.world(),hull));
            CHECK(fleet.piloted()==hull);
            CHECK(!fleet.reconcile(w.world()));
            CHECK(w.world().canonical_state()==before_select);
            CHECK(w.generation()==generation); // selection is not a cargo move
            CHECK(w.world().npc_records().size()==total_people);
            ships=fleet.list(w.world());
            CHECK(ships.size()==2 && ships[0].ready && ships[1].ready);
            CHECK(ships[1].selected && !ships[0].selected);
            CHECK(ships[1].crew==2);
            CHECK(ships[1].food_kg==200);
            CHECK(ships[1].water_kg==200);
            CHECK(ships[1].oxygen_kg==200);
            CHECK(w.world().total_ore()==conserved);
            CHECK(w.world().freighter_build_orders().at(92001).source_ship==origin);
            CHECK(w.world().colony_ship(hull)->crew==2);
            for(const auto pilot:pilots)
                CHECK(w.world().npc_records().at(pilot).home_ship==hull);

            // The view/focus is also owner scoped. Merely knowing the
            // other owner's real ship ID cannot make it the player's camera.
            bool denied=false;
            try{(void)ServerViewBridge(w.world(),815,7,foreign);}
            catch(const std::invalid_argument&){denied=true;}
            CHECK(denied);
            ServerViewBridge local(w.world(),816,7,hull);
            CHECK(visible(local.snapshot(),hull));
            CHECK(visible(local.snapshot(),origin));

            // Use real, delayed celestial transit; the minted hull and its
            // real inventory move while the founding colony stays in frame A.
            CHECK(w.begin_celestial_transit(7,hull,body,93001));
            w.steps(4);
            CHECK(w.complete_celestial_transit(93001));
            CHECK(!w.world().same_local_frame(origin,hull));
            CHECK(fleet.piloted()==hull);
            CHECK(!fleet.reconcile(w.world()));
            ServerViewBridge chosen(w.world(),817,7,hull);
            ServerViewBridge colony(w.world(),818,7,origin);
            const auto remote_frame=chosen.snapshot();
            const auto home_frame=colony.snapshot();
            CHECK(visible(remote_frame,hull));
            CHECK(!visible(remote_frame,origin)); // frame isolation
            CHECK(visible(home_frame,origin));
            CHECK(!visible(home_frame,hull)); // no remote cargo leak
            ClientView receiver;
            CHECK(receiver.apply(decode_frame(encode_frame(remote_frame)))==
                  ApplyResult::Applied);
            CHECK(receiver.world().entities.count(hull)==1);
            CHECK(receiver.world().entities.count(origin)==0);

            // Even while the user's cockpit is physically remote, every
            // existing yard order and founding-colony module belongs to A.
            CHECK(w.world().freighter_build_orders().at(92001).source_ship==origin);
            CHECK(w.world().shipyard_constructions().at(91001).colony_ship==origin);
            CHECK(w.world().total_ore()==conserved);
            durable=w.world().canonical_state();
            CHECK(Simulation::from_snapshot(durable).canonical_state()==durable);
        }
        {
            PersistentWorld loaded(db.string(),Simulation(881));
            CHECK(loaded.world().canonical_state()==durable);
            CHECK(loaded.world().freighter_build_orders().at(92001).source_ship==origin);
            // Restart resets the *host's* control plane, never the shipped
            // world's hull, NPCs or freight; browser may re-request the ID.
            FleetSelection fleet(7,origin);
            CHECK(fleet.piloted()==origin);
            CHECK(fleet.select(loaded.world(),hull));
            CHECK(!fleet.select(loaded.world(),foreign));
            CHECK(fleet.piloted()==hull);
            CHECK(!fleet.reconcile(loaded.world()));
        }
        fs::remove(db);
        std::cout<<"PASS SHIP-01D: native owned fleet selection, no teleporting resources, manufactured pilot, frame focus isolation and restart\n";
    }catch(const std::exception& e){
        std::error_code ec;fs::remove(db,ec);
        std::cerr<<"FAIL SHIP-01D "<<e.what()<<'\n';
        return 1;
    }
}
