#include "galaxy/Persistence.hpp"
#include "galaxy/WO5DelegatedLogistics.hpp"
#include "galaxy/WO5RegionalRecovery.hpp"
#include "galaxy/WO5RegionalFreightFence.hpp"
#include "galaxy/WO5FreightEvidence.hpp"
#include "galaxy/FleetSelection.hpp"
#include "galaxy/OrbitalShuttleCommands.hpp"
#include "galaxy/ShipInteractionExecutors.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("PLAY13D3: ")+#x+" line "+std::to_string(__LINE__));}while(false)
struct Chosen {std::uint32_t system=0;std::uint64_t body=0;std::uint16_t latitude=0,longitude=0;};
static Chosen select_site(bool farm) {
    for(std::uint32_t system=1;system<=2000;++system) {
        Simulation sample(613,system);
        const auto ship=sample.add_colony_ship(7,{0,0},60,8);
        sample.provision_starter_exploration_scanner(ship);
        sample.ensure_physical_celestial_systems();
        for(const auto& body:sample.physical_celestial_systems().at(system)) {
            if(body.kind!=CelestialKind::Planet && body.kind!=CelestialKind::Moon)continue;
            if(farm && (body.orbital_radius_milli_au<2200 ||
                        body.orbital_radius_milli_au>12000))continue;
            if(!farm) {
                const auto prospect=sample.celestial_oasis_prospect(body.id);
                if(!prospect || !prospect->candidate)continue;
                // Source lookup requires the same finite original genesis reserve
                // that scenario() installs before PersistentWorld is created.
                // Merely surveying a potential oasis does not create matter.
                sample.provision_oasis_reserve_genesis(body.id,8000,4000);
                const auto source=sample.planetary_oasis_source_region(7,ship,body.id);
                if(source)return {system,body.id,source->latitude,source->longitude};
                continue;
            }
            for(const auto [y,x]:std::vector<std::pair<unsigned,unsigned>>{
                {17,36},{17,18},{18,46},{16,60},{14,4},{20,32},{10,27},{26,52},
                {7,15},{28,8},{12,40},{24,55},{19,50},{15,64}}) {
                const auto region=sample.scan_planetary_geography(7,ship,body.id,
                    std::uint16_t(y),std::uint16_t(x));
                if(region && !region->landscape.region.surface_water &&
                   (!farm || region->landscape.region.farming_possible))
                    return {system,body.id,std::uint16_t(y),std::uint16_t(x)};
            }
        }
    }
    throw std::runtime_error("no suitable physically catalogued planetary region");
}
static void fabricate(PersistentWorld& w,EntityId ship) {
    auto apply=[&](std::uint64_t id,Action action,std::int64_t amount,EntityId target=0){
        Command c{};c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
        c.issuer=7;c.actor=ship;c.action=action;c.amount=amount;c.target=target;
        CHECK(w.submit(c));w.step();
        CHECK(w.world().events().back().result==Outcome::Applied);
    };
    // Use the existing verified 4-ore/2-module production cadence.
    apply(2001,Action::Mine,4,ship+1);
    apply(2002,Action::RefineOre,4);
    apply(2003,Action::FabricateModule,2);
    apply(2004,Action::Mine,4,ship+1);
    apply(2005,Action::RefineOre,4);
    apply(2006,Action::FabricateModule,2);
    // Third batch is fabricated before departure: the asteroid stays in
    // the original celestial frame and cannot be mined remotely from orbit.
    apply(2007,Action::Mine,4,ship+1);
    apply(2008,Action::RefineOre,4);
    apply(2009,Action::FabricateModule,2);
    CHECK(w.world().colony_ship(ship)->modules>=6);
}
// SHIP-01C: each batch is a real asteroid -> refinery -> module transfer.
static void fabricate_six_more_batches(PersistentWorld& w,EntityId ship,
                                        EntityId asteroid) {
    std::uint64_t id=2100;
    auto work=[&](Action action,std::int64_t amount,EntityId target=0){
        Command c{};c.id=++id;c.sequence=c.id;c.execute_tick=w.world().tick()+1;
        c.issuer=7;c.actor=ship;c.action=action;c.amount=amount;c.target=target;
        CHECK(w.submit(c));w.step();
        CHECK(w.world().events().back().result==Outcome::Applied);
    };
    for(int i=0;i<6;++i) {
        work(Action::Mine,4,asteroid);
        work(Action::RefineOre,4);
        work(Action::FabricateModule,2);
    }
}
// Negative recovery oracle: a fabricated coordinator completion must not
// pass v94 snapshot validation without its original native transport receipt.
static std::string forge_work_completion(std::string snapshot,
        std::uint64_t directive,std::uint64_t tick) {
    const auto marker="workdirective|"+std::to_string(directive)+"|";
    const auto at=snapshot.find(marker);
    CHECK(at!=std::string::npos);
    const auto end=snapshot.find('\n',at);
    CHECK(end!=std::string::npos);
    std::istringstream source(snapshot.substr(at,end-at));
    std::vector<std::string> fields;
    std::string token;
    while(std::getline(source,token,'|'))fields.push_back(token);
    CHECK(fields.size()==15);
    fields[10]="2"; // fake Fulfilled
    fields[11]="0"; // fake unblocked
    fields[14]=std::to_string(tick); // plausible forged resolved tick
    std::string row;
    for(std::size_t i=0;i<fields.size();++i){
        if(i)row+='|';
        row+=fields[i];
    }
    snapshot.replace(at,end-at,row);
    return snapshot;
}
// FLIGHT-01 recovery oracle: a completed flight intent is not an
// arrival receipt. The physical native destination/owner/request must exist.
static std::string forge_npc_flight_arrival(std::string snapshot,
        std::uint64_t order_id,std::uint64_t tick) {
    const auto marker="npcflight|"+std::to_string(order_id)+"|";
    const auto start=snapshot.find(marker);
    CHECK(start!=std::string::npos);
    const auto end=snapshot.find('\n',start);
    CHECK(end!=std::string::npos);
    std::istringstream stream(snapshot.substr(start,end-start));
    std::vector<std::string> fields;
    std::string word;
    while(std::getline(stream,word,'|'))fields.push_back(word);
    CHECK(fields.size()==12);
    fields[8]="2"; // claimed Arrived without native completed transit
    fields[11]=std::to_string(tick);
    std::string row;
    for(std::size_t i=0;i<fields.size();++i){
        if(i)row+='|';
        row+=fields[i];
    }
    snapshot.replace(start,end-start,row);
    return snapshot;
}
static void scenario(const fs::path& db, bool farming,
                     bool manufactured=false,bool delegated=false,
                     bool autonomous=false,bool rendezvous_freight=false){
    const auto chosen=select_site(farming);
    EntityId ship=0,freighter=0,receiver=0;
    std::uint64_t receiver_body=0;
    std::uint64_t first_npc=0,second_npc=0;
    std::int64_t origin=0;
    std::string durable;
    std::uint64_t verified_ascent_receipt=0;
    {
        Simulation seed(613,chosen.system);
        ship=seed.add_colony_ship(7,{0,0},60,manufactured?10:8);
        seed.provision_colony_genesis(ship,2000,2000,2000,2000,2000);
        // Ordinary PLAY-13D tests retain the preexisting crewed carrier.
        // SHIP-01C instead builds its one freighter from the initial asteroid.
        seed.add_asteroid({2000,0},manufactured?120:60);
        if(!manufactured)
            freighter=seed.add_colony_ship(7,{0,0},60,3);
        receiver=seed.add_colony_ship(7,{0,0},60,3);
        if(!manufactured)
            seed.provision_colony_genesis(freighter,1000,1000,1000,1000,1000);
        seed.provision_colony_genesis(receiver,1000,1000,1000,1000,1000);
        // Named crew must be materialized before scanner provisioning upgrades
        // the original world beyond the genesis-only workforce migration.
        seed.enable_individual_workforce();
        seed.provision_starter_exploration_scanner(ship);
        if(!manufactured)seed.provision_starter_exploration_scanner(freighter);
        // PLAY-13C arrival never bypasses the actual fitted-scanner gate:
        // Colony B's pre-existing receiving ship must carry its own survey
        // module before the test may move it to a remote celestial frame.
        seed.provision_starter_exploration_scanner(receiver);
        seed.ensure_physical_celestial_systems();
        for(const auto& body:seed.physical_celestial_systems().at(chosen.system))
            if(body.id!=chosen.body &&
               (body.kind==CelestialKind::Planet ||
                body.kind==CelestialKind::Moon)) {
                receiver_body=body.id;break;
            }
        CHECK(receiver_body!=0);
        if(!farming)seed.provision_oasis_reserve_genesis(chosen.body,8000,4000);
        seed.ensure_ship_frame_registry();
        origin=seed.total_ore();
        PersistentWorld w(db.string(),std::move(seed));
        FleetSelection fleet(7,ship); // home is always industrial authority
        CHECK(fleet.home()==ship && fleet.piloted()==ship);
        fabricate(w,ship);
        if(manufactured){
            std::vector<std::uint64_t> people;
            for(const auto& [id,n]:w.world().npc_records())
                if(n.home_ship==ship && n.location==ship &&
                   n.role==NpcRole::Ship)people.push_back(id);
            CHECK(people.size()==10);
            const std::array<std::uint64_t,2> builders{{people[0],people[1]}};
            const std::array<std::uint64_t,2> operating{{people[2],people[3]}};
            const auto initial_people=w.world().npc_records().size();
            CHECK(w.start_shipyard_construction(7,ship,91001,builders));
            w.steps(8);
            CHECK(w.world().shipyard_constructions().at(91001).operational());
            fabricate_six_more_batches(w,ship,ship+1);
            CHECK(w.queue_shipyard_freighter(7,91001,92001));
            CHECK(!w.queue_shipyard_freighter(7,91001,92002));
            w.steps(12);
            const auto& built=w.world().freighter_build_orders().at(92001);
            CHECK(built.finished());
            freighter=built.launched_ship;
            CHECK(freighter && freighter!=ship && freighter!=receiver);
            CHECK(w.world().find(freighter)->manufactured_order_id==92001);
            CHECK(w.world().find(freighter)->cargo_capacity==100);
            CHECK(!w.world().colony_ship(freighter));
            CHECK(!w.world().find(freighter)->fitting.exploration_scanner());
            CHECK(!w.begin_celestial_transit(7,freighter,chosen.body,92004));
            CHECK(w.commission_shipyard_freighter(7,92001,operating,250));
            CHECK(w.world().freighter_build_orders().at(92001).commissioned());
            CHECK(fleet.is_ready(w.world(),freighter));
            CHECK(fleet.piloted()==ship);
            CHECK(w.world().colony_ship(freighter)->crew==2);
            CHECK(w.world().life_support(freighter)->food_kg==250);
            CHECK(w.world().life_support(freighter)->water_kg==250);
            CHECK(w.world().life_support(freighter)->oxygen_kg==250);
            CHECK(w.world().find(freighter)->fitting.exploration_scanner());
            CHECK(w.world().npc_records().size()==initial_people);
            CHECK(w.world().npc_records().at(operating[0]).home_ship==freighter);
            CHECK(w.world().npc_records().at(operating[1]).home_ship==freighter);
            CHECK(w.world().total_ore()==origin);
            const auto commissioned=w.world().canonical_state();
            CHECK(commissioned.rfind("galaxy-kernel-v92|",0)==0);
            CHECK(Simulation::from_snapshot(commissioned).canonical_state()==commissioned);
        }
        CHECK(w.begin_celestial_transit(7,ship,chosen.body,90001));
        w.steps(4);
        CHECK(w.complete_celestial_transit(90001));
        // Both vessels physically navigate via the existing journaled frame
        // transit. The destination remains in a *different* celestial frame.
        CHECK(w.begin_celestial_transit(7,freighter,chosen.body,90012));
        w.steps(4);
        CHECK(w.complete_celestial_transit(90012));
        CHECK(w.begin_celestial_transit(7,receiver,receiver_body,90013));
        w.steps(4);
        CHECK(w.complete_celestial_transit(90013));
        CHECK(w.world().total_ore()==origin);
        std::vector<std::uint64_t> available;
        for(const auto& [id,n]:w.world().npc_records())
            if(n.home_ship==ship && n.location==ship && n.role==NpcRole::Ship)
                available.push_back(id);
        CHECK(available.size()>=3);
        first_npc=available[0];second_npc=available[1];
        CHECK(!w.build_planetary_site(7,ship,chosen.body,
            chosen.latitude,chosen.longitude,first_npc,first_npc));
        CHECK(!w.build_planetary_site(7,ship,chosen.body,
            chosen.latitude,chosen.longitude,first_npc,999999999));
        CHECK(!w.build_planetary_site(8,ship,chosen.body,chosen.latitude,chosen.longitude));
        auto pre=w.world().canonical_state();
        bool failed=false;
        try{(void)w.build_planetary_site(7,ship,chosen.body,
            chosen.latitude,chosen.longitude,FailPoint::AfterSnapshotWrite);}
        catch(const std::exception&){failed=true;}
        CHECK(failed);
        CHECK(w.world().canonical_state()==pre);
        CHECK(w.build_planetary_site(7,ship,chosen.body,chosen.latitude,chosen.longitude,
                                     first_npc,second_npc));
        CHECK(!w.build_planetary_site(7,ship,chosen.body,chosen.latitude,chosen.longitude));
        const auto key=PlanetarySiteKey{chosen.body,chosen.latitude,chosen.longitude,1};
        CHECK(!w.world().planetary_spaceport_operational(chosen.body,chosen.latitude,chosen.longitude));
        CHECK(!w.equip_planetary_site(7,ship,chosen.body,chosen.latitude,chosen.longitude,farming));
        CHECK(w.world().planetary_sites().at(key).assigned_workers==2);
        const auto& workers=w.world().planetary_sites().at(key).delegated_npcs;
        CHECK(workers[0] && workers[1] && workers[0]!=workers[1]);
        CHECK(workers[0]==first_npc && workers[1]==second_npc);
        for(const auto npc:workers){
            const auto custody=w.world().npc_custody(npc);
            CHECK(custody.home_ship==ship);
            CHECK(custody.physical_location==
                  w.world().planetary_sites().at(key).surface_location);
            CHECK(custody.assigned_role==NpcRole::PlanetarySurface);
        }
        CHECK(w.world().colony_ship(ship)->crew==6);
        w.steps(4);
        CHECK(w.world().planetary_sites().at(key).spaceport_phase==2);
        CHECK(!w.support_planetary_crew(8,ship,chosen.body,
            chosen.latitude,chosen.longitude,false,10));
        CHECK(w.support_planetary_crew(7,ship,chosen.body,
            chosen.latitude,chosen.longitude,false,10));
        CHECK(w.support_planetary_crew(7,ship,chosen.body,
            chosen.latitude,chosen.longitude,true,10));
        CHECK(w.world().total_ore()==origin);
        w.steps(239);
        CHECK(!w.world().planetary_spaceport_operational(chosen.body,chosen.latitude,chosen.longitude));
        w.step();
        CHECK(w.world().planetary_spaceport_operational(chosen.body,chosen.latitude,chosen.longitude));
        // A v78 autonomous-disabled world is still canonical and reloadable.
        CHECK(Simulation::from_snapshot(w.world().canonical_state()).canonical_state()
              ==w.world().canonical_state());
        CHECK(w.equip_planetary_site(7,ship,chosen.body,chosen.latitude,chosen.longitude,farming));
        CHECK(!w.equip_planetary_site(7,ship,chosen.body,chosen.latitude,chosen.longitude,farming));
        CHECK(w.world().colony_ship(ship)->modules==(manufactured?3:2));

        // PLAY-13D.4C.2: the extra modules were fabricated before
        // departure, not mined from an asteroid in a different local frame.
        CHECK(w.world().colony_ship(ship)->modules==(manufactured?3:2));
        CHECK(!w.build_planetary_shuttle(8,ship,key));
        const auto shuttle_before=w.world().canonical_state();
        bool shuttle_failed=false;
        try{(void)w.build_planetary_shuttle(7,ship,key,FailPoint::AfterSnapshotWrite);}
        catch(const std::exception&){shuttle_failed=true;}
        CHECK(shuttle_failed);
        CHECK(w.world().canonical_state()==shuttle_before);
        CHECK(w.build_planetary_shuttle(7,ship,key));
        CHECK(!w.build_planetary_shuttle(7,ship,key));
        CHECK(w.world().colony_ship(ship)->modules==(manufactured?1:0));
        CHECK(w.world().planetary_shuttles().at(key).dry_mass_ore==4);
        // PLAY-13D.4C.6: the standalone desktop client can issue typed native
        // commands without HTTP, relay JSON, or a second gameplay authority.
        OrbitalShuttleCommand native{};
        native.owner=7;native.ship=ship;native.site=key;
        native.action=OrbitalShuttleAction::Status;
        const auto native_generation=w.generation();
        const auto native_status=execute_orbital_shuttle(w,native);
        CHECK(native_status.accepted && !native_status.mutated);
        CHECK(native_status.present && native_status.shuttle.dry_mass_ore==4);
        CHECK(native_status.generation==native_generation);
        CHECK(native_status.goods_manifest && native_status.goods_manifest->valid());
        CHECK(native_status.goods_manifest->contents.empty());
        const transport::Endpoint orbit_endpoint{transport::LocationKind::ColonyShip,ship};
        CHECK(native_status.goods_manifest->berth==orbit_endpoint);
        native.owner=8;
        CHECK(!execute_orbital_shuttle(w,native).accepted);
        native.owner=7;
        native.site=PlanetarySiteKey{chosen.body,chosen.latitude,
            std::uint16_t((chosen.longitude+1)%72),1};
        CHECK(!execute_orbital_shuttle(w,native).accepted);
        native.site=key;
        native.action=OrbitalShuttleAction::Load;
        native.kilograms=-1;
        CHECK(!execute_orbital_shuttle(w,native).accepted);
        native.kilograms=1001;
        CHECK(!execute_orbital_shuttle(w,native).accepted);
        CHECK(w.generation()==native_generation);
        CHECK(w.world().total_ore()==origin);

        CHECK(!w.assign_planetary_shuttle_pilot(7,ship,key,first_npc));
        CHECK(!w.assign_planetary_shuttle_pilot(7,ship,key,9999999));
        CHECK(w.assign_planetary_shuttle_pilot(7,ship,key,available[2]));
        CHECK(w.world().npc_custody(available[2]).physical_location==ship);
        CHECK(!w.load_planetary_shuttle(8,ship,key,true,10));
        CHECK(!w.load_planetary_shuttle(7,ship,key,true,1001));
        const auto load_before=w.world().canonical_state();
        bool load_failed=false;
        try{(void)w.load_planetary_shuttle(7,ship,key,true,200,
                                          FailPoint::AfterSnapshotWrite);}
        catch(const std::exception&){load_failed=true;}
        CHECK(load_failed);
        CHECK(w.world().canonical_state()==load_before);
        const auto before_native_load=w.generation();
        native.action=OrbitalShuttleAction::Load;
        native.kilograms=200;
        native.water=true;
        const auto native_load=execute_orbital_shuttle(w,native);
        CHECK(native_load.accepted && native_load.mutated &&
              native_load.shuttle.water_kg==200 &&
              native_load.generation==before_native_load+1);
        CHECK(w.load_planetary_shuttle(7,ship,key,false,100));
        CHECK(w.world().planetary_shuttles().at(key).water_kg==200);
        CHECK(w.world().planetary_shuttles().at(key).food_kg==100);
        const auto mixed=w.world().planetary_transport_manifest(key);
        CHECK(mixed && mixed->valid());
        CHECK(transport::contains_good(*mixed,transport::GoodKind::Resource,
            static_cast<std::uint8_t>(transport::ResourceGood::Water),200));
        CHECK(transport::contains_good(*mixed,transport::GoodKind::Resource,
            static_cast<std::uint8_t>(transport::ResourceGood::Food),100));
        CHECK(!w.unload_planetary_shuttle(7,ship,key,true,201));
        CHECK(w.unload_planetary_shuttle(7,ship,key,true,50));
        CHECK(w.world().planetary_shuttles().at(key).water_kg==150);
        CHECK(w.world().total_ore()==origin);
        CHECK(w.world().canonical_state().rfind((manufactured?"galaxy-kernel-v92|":"galaxy-kernel-v80|"),0)==0);
        CHECK(Simulation::from_snapshot(w.world().canonical_state())
              .canonical_state()==w.world().canonical_state());

        // PLAY-13D.4C.7: reserve real named ferry-style passengers in
        // the SAME NPC roster. Booking and cancellation are atomic SQLite
        // mutations and cannot invent a second physical worker.
        CHECK(available.size()>=5);
        const auto passenger=available[3];
        CHECK(!w.book_planetary_shuttle_passenger(8,ship,key,passenger,true));
        CHECK(!w.book_planetary_shuttle_passenger(7,ship,key,available[2],true));
        CHECK(w.book_planetary_shuttle_passenger(7,ship,key,passenger,true));
        CHECK(!w.book_planetary_shuttle_passenger(7,ship,key,passenger,true));
        CHECK(!w.world().npc_available_for_duty(passenger));
        CHECK(w.world().planetary_shuttles().at(key).passenger_ids==
              std::vector<std::uint64_t>{passenger});
        CHECK(w.world().canonical_state().rfind((manufactured?"galaxy-kernel-v92|":"galaxy-kernel-v82|"),0)==0);
        const auto booked=w.world().canonical_state();
        CHECK(Simulation::from_snapshot(booked).canonical_state()==booked);
        CHECK(w.book_planetary_shuttle_passenger(7,ship,key,passenger,false));
        CHECK(w.world().planetary_shuttles().at(key).passenger_ids.empty());
        const auto reservation_before=w.world().canonical_state();
        bool reservation_failed=false;
        try{(void)w.book_planetary_shuttle_passenger(
                7,ship,key,passenger,true,FailPoint::AfterSnapshotWrite);}
        catch(const std::exception&){reservation_failed=true;}
        CHECK(reservation_failed);
        CHECK(w.world().canonical_state()==reservation_before);
        native.action=OrbitalShuttleAction::BookPassenger;
        native.kilograms=0;native.npc=passenger;
        const auto booked_native=execute_orbital_shuttle(w,native);
        CHECK(booked_native.accepted && booked_native.mutated &&
              booked_native.shuttle.passenger_ids.size()==1);
        CHECK(booked_native.goods_manifest && booked_native.goods_manifest->valid());
        CHECK(transport::contains_good(*booked_native.goods_manifest,
            transport::GoodKind::Npc,passenger,1));
        native.npc=0;

        // PLAY-13D.4C.3: the named pilot moves using the SAME NPC roster
        // as migration and ferry operations, never a shadow shuttle person.
        CHECK(!w.launch_planetary_shuttle(8,ship,key,81001,false));
        CHECK(!w.launch_planetary_shuttle(7,ship,key,0,false));
        const auto departure_before=w.world().canonical_state();
        bool departure_failed=false;
        try{(void)w.launch_planetary_shuttle(7,ship,key,81001,false,
                                             FailPoint::AfterSnapshotWrite);}
        catch(const std::exception&){departure_failed=true;}
        CHECK(departure_failed);
        CHECK(w.world().canonical_state()==departure_before);
        native.action=OrbitalShuttleAction::Descend;
        native.kilograms=0;
        native.request_id=81001;
        const auto native_departure=execute_orbital_shuttle(w,native);
        CHECK(native_departure.accepted && native_departure.mutated &&
              native_departure.shuttle.flight_phase==1 &&
              native_departure.shuttle.flight_request==81001);
        CHECK(!w.launch_planetary_shuttle(7,ship,key,81001,false));
        CHECK(w.world().planetary_shuttles().at(key).flight_phase==1);
        CHECK(w.world().npc_custody(available[2]).physical_location!=ship);
        CHECK(w.world().npc_custody(available[2]).passenger);
        CHECK(w.world().npc_custody(available[2]).active_transport_id==81001);
        CHECK(w.world().npc_custody(passenger).physical_location!=ship);
        CHECK(w.world().npc_custody(passenger).passenger);
        CHECK(w.world().npc_custody(passenger).active_transport_id==81001);
        CHECK(!w.book_planetary_shuttle_passenger(7,ship,key,passenger,false));
        CHECK(!w.unload_planetary_shuttle(7,ship,key,true,1));
        CHECK(!w.load_planetary_shuttle(7,ship,key,false,1));
        CHECK(!w.begin_celestial_transit(7,ship,chosen.body,81002));
        CHECK(w.world().total_ore()==origin);
        const auto in_flight=w.world().canonical_state();
        CHECK(in_flight.rfind((manufactured?"galaxy-kernel-v92|":"galaxy-kernel-v82|"),0)==0);
        CHECK(Simulation::from_snapshot(in_flight).canonical_state()==in_flight);
        w.steps(3);
        CHECK(w.world().planetary_shuttles().at(key).flight_phase==2);
        if(available.size()>3)
            CHECK(!w.assign_planetary_shuttle_pilot(7,ship,key,available[3]));
        CHECK(w.world().planetary_shuttles().at(key).descent_receipt==81001);
        CHECK(w.world().npc_custody(available[2]).physical_location==
              w.world().planetary_sites().at(key).surface_location);
        CHECK(w.world().npc_custody(passenger).physical_location==
              w.world().planetary_sites().at(key).surface_location);
        CHECK(!w.world().npc_available_for_duty(passenger));
        // PLAY-13D.4C.8: disembark preserves the same finite NPC at
        // the surface without silently granting permanent immigration.
        CHECK(!w.transfer_planetary_shuttle_visitor(8,ship,key,passenger,false));
        CHECK(w.transfer_planetary_shuttle_visitor(7,ship,key,passenger,false));
        CHECK(!w.transfer_planetary_shuttle_visitor(7,ship,key,passenger,false));
        CHECK(w.world().planetary_shuttles().at(key).passenger_ids.empty());
        CHECK(w.world().planetary_shuttles().at(key).disembarked_ids==
              std::vector<std::uint64_t>{passenger});
        CHECK(w.world().npc_custody(passenger).physical_location==
              w.world().planetary_sites().at(key).surface_location);
        CHECK(w.world().canonical_state().rfind((manufactured?"galaxy-kernel-v92|":"galaxy-kernel-v85|"),0)==0);
        CHECK(w.world().planetary_shuttles().at(key).visitor_arrival_receipts.at(passenger)==81001);
        const auto verified_arrival=w.world().npc_transport_arrival(81001,passenger);
        CHECK(verified_arrival && verified_arrival->mode()==transport::Mode::PlanetaryShuttle);
        CHECK(transport::completed_transport(*verified_arrival,passenger));
        CHECK(!w.world().npc_transport_arrival(81001,available[2]));
        CHECK(!w.world().npc_transport_arrival(81001,9999999));
        const auto visitor_state=w.world().canonical_state();
        CHECK(Simulation::from_snapshot(visitor_state).canonical_state()==visitor_state);
        // v86 legal residence is an independent persistent address, not an
        // alias for physical shuttle presence or a temporary work duty.
        auto upgraded_residence=Simulation::from_snapshot(visitor_state);
        const transport::Endpoint expected_home{
            transport::LocationKind::ColonyShip,ship};
        CHECK(upgraded_residence.npc_records().at(passenger).legal_residence()==
              expected_home);
        // The ordinary v85 route needs the explicit v86 migration. The
        // manufactured shipyard path already advanced to v92 with legal
        // residence persisted; never downgrade its canonical world.
        // Only historical v85 worlds require the one-time native migration.
        // A manufactured or supervised NPC freight scenario may already be
        // v92/v95; migration must never downgrade its original receipts.
        const bool legacy_v85=visitor_state.rfind("galaxy-kernel-v85|",0)==0;
        if(legacy_v85)
            upgraded_residence.upgrade_legal_residence_snapshot_format();
        const auto v86_state=upgraded_residence.canonical_state();
        CHECK(v86_state.rfind(legacy_v85?"galaxy-kernel-v86|":
              visitor_state.substr(0,visitor_state.find('|')+1),0)==0);
        CHECK(Simulation::from_snapshot(v86_state).canonical_state()==v86_state);
        CHECK(Simulation::from_snapshot(v86_state).npc_records().at(passenger)
              .legal_residence()==expected_home);
        const auto census_v86=upgraded_residence.assess_npc_residence();
        CHECK(census_v86.reconciled);
        CHECK(census_v86.total_living==upgraded_residence.npc_records().size());
        CHECK(census_v86.living_by_residence.at(expected_home)>=1);
        const auto surface_id=w.world().planetary_sites().at(key).surface_location;
        CHECK(census_v86.physically_present.at(surface_id)>=1);
        CHECK(Simulation::from_snapshot(v86_state).assess_npc_residence().reconciled);
        // A syntactically valid v86 address with no registered settlement
        // cannot be smuggled in by editing a save.
        auto forged_residence=v86_state;
        const auto npc_line=forged_residence.find(
            "npc|"+std::to_string(passenger)+"|");
        CHECK(npc_line!=std::string::npos);
        const auto npc_end=forged_residence.find('\n',npc_line);
        CHECK(npc_end!=std::string::npos);
        const auto last_bar=forged_residence.rfind('|',npc_end);
        const auto kind_bar=forged_residence.rfind('|',last_bar-1);
        CHECK(kind_bar!=std::string::npos);
        forged_residence.replace(kind_bar+1,last_bar-kind_bar-1,"4");
        bool unknown_residence_rejected=false;
        try{(void)Simulation::from_snapshot(forged_residence);}
        catch(const std::exception&){unknown_residence_rejected=true;}
        CHECK(unknown_residence_rejected);

        // Temporary work is an explicit separate order, not a side effect
        // of arriving at a planetary spaceport.
        CHECK(!w.assign_planetary_shuttle_visitor_duty(8,ship,key,passenger,true));
        CHECK(w.assign_planetary_shuttle_visitor_duty(7,ship,key,passenger,true));
        CHECK(!w.assign_planetary_shuttle_visitor_duty(7,ship,key,passenger,true));
        CHECK(w.world().planetary_shuttles().at(key).duty_ids==
              std::vector<std::uint64_t>{passenger});
        CHECK(w.world().canonical_state().rfind((manufactured?"galaxy-kernel-v92|":"galaxy-kernel-v85|"),0)==0);
        const auto assigned=w.world().canonical_state();
        CHECK(Simulation::from_snapshot(assigned).canonical_state()==assigned);
        const auto failed_duty_before=w.world().canonical_state();
        bool duty_failed=false;
        try{(void)w.assign_planetary_shuttle_visitor_duty(
            7,ship,key,passenger,false,FailPoint::AfterSnapshotWrite);}
        catch(const std::exception&){duty_failed=true;}
        CHECK(duty_failed && w.world().canonical_state()==failed_duty_before);
        const auto failed_transfer_before=w.world().canonical_state();
        bool failed_transfer=false;
        try{(void)w.transfer_planetary_shuttle_visitor(
            7,ship,key,passenger,true,FailPoint::AfterSnapshotWrite);}
        catch(const std::exception&){failed_transfer=true;}
        CHECK(failed_transfer && w.world().canonical_state()==failed_transfer_before);
        CHECK(!w.launch_planetary_shuttle(7,ship,key,81001,true));
        CHECK(w.unload_planetary_shuttle(7,ship,key,true,60));
        CHECK(w.unload_planetary_shuttle(7,ship,key,false,20));
        CHECK(w.load_planetary_shuttle(7,ship,key,true,50));
        CHECK(w.world().total_ore()==origin);
        const auto land_snapshot=w.world().canonical_state();
        CHECK(Simulation::from_snapshot(land_snapshot).canonical_state()==land_snapshot);
        CHECK(w.launch_planetary_shuttle(7,ship,key,81003,true));
        CHECK(!w.launch_planetary_shuttle(7,ship,key,81003,true));
        CHECK(w.world().planetary_shuttles().at(key).flight_phase==3);
        CHECK(w.world().total_ore()==origin);
        CHECK(Simulation::from_snapshot(w.world().canonical_state())
              .canonical_state()==w.world().canonical_state());
        w.steps(3);
        CHECK(w.world().planetary_shuttles().at(key).flight_phase==0);
        CHECK(w.world().planetary_shuttles().at(key).ascent_receipt==81003);
        CHECK(w.world().npc_custody(available[2]).physical_location==ship);
        CHECK(!w.launch_planetary_shuttle(7,ship,key,81001,false));
        CHECK(!w.launch_planetary_shuttle(7,ship,key,81003,false));
        CHECK(w.unload_planetary_shuttle(7,ship,key,true,50));
        CHECK(w.world().total_ore()==origin);
        CHECK(Simulation::from_snapshot(w.world().canonical_state())
              .canonical_state()==w.world().canonical_state());

        CHECK(w.world().npc_custody(passenger).physical_location==
              w.world().planetary_sites().at(key).surface_location);
        CHECK(w.world().planetary_shuttles().at(key).disembarked_ids==
              std::vector<std::uint64_t>{passenger});
        // The shuttle can return to its standing surface port to collect
        // the exact passenger later; there is no teleport or free return.
        CHECK(!w.transfer_planetary_shuttle_visitor(7,ship,key,passenger,true));
        CHECK(w.launch_planetary_shuttle(7,ship,key,81004,false));
        w.steps(3);
        CHECK(w.world().planetary_shuttles().at(key).flight_phase==2);
        // Individual custody remains bound to the original 81001 descent,
        // not the later unrelated 81004 shuttle flight.
        CHECK(!w.world().npc_transport_arrival(81004,passenger));
        CHECK(w.world().npc_transport_arrival(81001,passenger));
        // A later landing must not invalidate the original visitor's
        // temporary duty. Both revocation and reassignment still work.
        CHECK(w.assign_planetary_shuttle_visitor_duty(7,ship,key,passenger,false));
        CHECK(w.assign_planetary_shuttle_visitor_duty(7,ship,key,passenger,true));
        native.action=OrbitalShuttleAction::Reboard;
        native.npc=passenger;native.request_id=0;
        const auto return_boarding=execute_orbital_shuttle(w,native);
        CHECK(return_boarding.accepted && return_boarding.mutated);
        CHECK(return_boarding.shuttle.passenger_ids==
              std::vector<std::uint64_t>{passenger});
        CHECK(return_boarding.shuttle.disembarked_ids.empty());
        CHECK(return_boarding.shuttle.duty_ids.empty());
        CHECK(return_boarding.shuttle.visitor_arrival_receipts.empty());
        CHECK(!w.world().npc_transport_arrival(81001,passenger));
        CHECK(!w.transfer_planetary_shuttle_visitor(7,ship,key,passenger,true));
        CHECK(w.launch_planetary_shuttle(7,ship,key,81005,true));
        CHECK(w.world().npc_custody(passenger).active_transport_id==81005);
        w.steps(3);
        CHECK(w.world().npc_custody(passenger).physical_location==ship);
        CHECK(w.world().planetary_shuttles().at(key).passenger_ids==
              std::vector<std::uint64_t>{passenger});
        CHECK(w.book_planetary_shuttle_passenger(7,ship,key,passenger,false));
        CHECK(w.world().npc_available_for_duty(passenger));
        if(!farming) {
            const auto source=w.world().planetary_oasis_source_region(7,ship,chosen.body);
            CHECK(source && source->latitude==chosen.latitude &&
                  source->longitude==chosen.longitude);
        }
        if(farming) {
            CHECK(w.supply_planetary_site(7,ship,chosen.body,chosen.latitude,chosen.longitude,true,100));
            CHECK(w.supply_planetary_site(7,ship,chosen.body,chosen.latitude,chosen.longitude,false,50));
            CHECK(!w.cultivate_planetary_site(8,ship,chosen.body,chosen.latitude,chosen.longitude,2));
            CHECK(w.cultivate_planetary_site(7,ship,chosen.body,chosen.latitude,chosen.longitude,2));
        }else{
            CHECK(!w.extract_planetary_site(7,ship,chosen.body,chosen.latitude,chosen.longitude,true,9000));
            CHECK(w.extract_planetary_site(7,ship,chosen.body,chosen.latitude,chosen.longitude,true,1000));
            CHECK(w.world().oasis_reserves().at(chosen.body).water_kg==7000);
        }
        const auto& region=w.world().planetary_sites().at(key);
        CHECK(region.owner==7&&region.source_ship==ship&&region.embodied_ore==8);
        if(farming) {
            CHECK(region.food_kg>=24 && region.wastewater_kg>=6);
            CHECK(region.harvest_cycles==2);
            CHECK(w.retrieve_planetary_site(7,ship,chosen.body,chosen.latitude,chosen.longitude,true,24));
        } else {
            CHECK(region.water_kg>=1000);
            CHECK(w.retrieve_planetary_site(7,ship,chosen.body,chosen.latitude,chosen.longitude,false,1000));
            CHECK(w.world().oasis_reserves().at(chosen.body).water_kg==7000);
        }
        CHECK(w.world().total_ore()==origin);
        // PLAY-13D.4B: the named NPC team works without repeated production
        // clicks, but cannot synthesize raw water or abandon matter custody.
        CHECK(!w.set_planetary_workforce_policy(8,chosen.body,
            chosen.latitude,chosen.longitude,1));
        CHECK(!w.set_planetary_workforce_policy(7,chosen.body,
            chosen.latitude,chosen.longitude,5));
        CHECK(!w.set_planetary_workforce_policy(7,chosen.body,
            chosen.latitude,chosen.longitude,farming?2:1));
        CHECK(w.supply_planetary_site(7,ship,chosen.body,
            chosen.latitude,chosen.longitude,true,350));
        CHECK(w.supply_planetary_site(7,ship,chosen.body,
            chosen.latitude,chosen.longitude,false,100));
        CHECK(w.support_planetary_crew(7,ship,chosen.body,
            chosen.latitude,chosen.longitude,false,150));
        CHECK(w.support_planetary_crew(7,ship,chosen.body,
            chosen.latitude,chosen.longitude,true,150));
        CHECK(w.world().total_ore()==origin);
        const auto policy=std::uint8_t(farming?1:2);
        const auto before_policy=w.world().canonical_state();
        bool policy_failed=false;
        try{(void)w.set_planetary_workforce_policy(7,chosen.body,
            chosen.latitude,chosen.longitude,policy,FailPoint::AfterSnapshotWrite);}
        catch(const std::exception&){policy_failed=true;}
        CHECK(policy_failed);
        CHECK(w.world().canonical_state()==before_policy);
        CHECK(w.set_planetary_workforce_policy(7,chosen.body,
            chosen.latitude,chosen.longitude,policy));
        CHECK(w.world().canonical_state().rfind((manufactured?"galaxy-kernel-v92|":"galaxy-kernel-v85|"),0)==0);
        CHECK(Simulation::from_snapshot(w.world().canonical_state())
              .planetary_sites().at(key).workforce_policy==policy);
        const auto before_site=w.world().planetary_sites().at(key);
        const auto reserve_before=!farming?
            w.world().oasis_reserves().at(chosen.body).water_kg:0;
        w.steps(100);
        const auto after_site=w.world().planetary_sites().at(key);
        CHECK(after_site.autonomous_cycles>=before_site.autonomous_cycles+4);
        CHECK(after_site.equipment_condition<100);
        if(farming){
            CHECK(after_site.harvest_cycles>before_site.harvest_cycles);
            CHECK(after_site.nutrients_kg<before_site.nutrients_kg);
        }else{
            CHECK(after_site.auto_extracted_kg>=4000);
            CHECK(w.world().oasis_reserves().at(chosen.body).water_kg<
                  reserve_before);
            CHECK(reserve_before-w.world().oasis_reserves().at(chosen.body).water_kg==
                  std::int64_t(after_site.auto_extracted_kg));
        }
        CHECK(w.world().total_ore()==origin);
        CHECK(w.set_planetary_workforce_policy(7,chosen.body,
            chosen.latitude,chosen.longitude,4));
        const auto repairs_before=after_site.repair_cycles;
        w.steps(20);
        CHECK(w.world().planetary_sites().at(key).repair_cycles>repairs_before);
        CHECK(w.world().planetary_sites().at(key).equipment_condition==100);
        CHECK(w.world().total_ore()==origin);
        CHECK(w.set_planetary_workforce_policy(7,chosen.body,
            chosen.latitude,chosen.longitude,0));
        const auto stopped=w.world().planetary_sites().at(key).autonomous_cycles;
        w.steps(40);
        CHECK(w.world().planetary_sites().at(key).autonomous_cycles==stopped);
        CHECK(w.world().total_ore()==origin);
        // Leaving the planet must neither delete its completed structure
        // nor permit remote inventory use from another celestial frame.
        const auto& system_bodies=w.world().physical_celestial_systems().at(chosen.system);
        std::uint64_t remote=0;
        for(const auto& candidate:system_bodies)
            if(candidate.id!=chosen.body){remote=candidate.id;break;}
        CHECK(remote!=0);
        CHECK(w.begin_celestial_transit(7,ship,remote,90002));
        w.steps(4);
        CHECK(w.complete_celestial_transit(90002));
        CHECK(!w.supply_planetary_site(7,ship,chosen.body,
            chosen.latitude,chosen.longitude,true,1));
        CHECK(!w.support_planetary_crew(7,ship,chosen.body,
            chosen.latitude,chosen.longitude,true,1));
        // Administrative policy can be changed remotely, while the same
        // finite NPC workforce continues working on its original planet.
        CHECK(w.set_planetary_workforce_policy(7,chosen.body,
            chosen.latitude,chosen.longitude,policy));
        const auto remote_before=w.world().planetary_sites().at(key).autonomous_cycles;
        w.steps(40);
        CHECK(w.world().planetary_sites().at(key).autonomous_cycles>remote_before);
        if(!farming) {
            // Once the finite reserve is emptied, no further kilograms
            // appear, even while the NPC extraction policy remains active.
            CHECK(w.world().oasis_reserves().at(chosen.body).water_kg==0);
            const auto extracted=w.world().planetary_sites().at(key).auto_extracted_kg;
            const auto stalls=w.world().planetary_sites().at(key).stalled_cycles;
            w.steps(40);
            CHECK(w.world().planetary_sites().at(key).auto_extracted_kg==extracted);
            CHECK(w.world().planetary_sites().at(key).stalled_cycles>stalls);
            CHECK(w.world().oasis_reserves().at(chosen.body).water_kg==0);
            CHECK(w.world().total_ore()==origin);
        }
        CHECK(w.set_planetary_workforce_policy(7,chosen.body,
            chosen.latitude,chosen.longitude,0));
        CHECK(w.world().planetary_sites().size()==1);
        CHECK(w.world().total_ore()==origin);
        durable=w.world().canonical_state();
        CHECK(durable.rfind((manufactured?"galaxy-kernel-v92|":"galaxy-kernel-v85|"),0)==0);
        CHECK(Simulation::from_snapshot(durable).canonical_state()==durable);
        if(!farming) {
            // Actual player-ordered planetary output recovery. The ship has
            // already left this body: loading there must fail before the
            // player returns, and nutrient stock cannot teleport to orbit.
            CHECK(!w.load_planetary_shuttle(7,ship,key,
                ShuttleCargo::Nutrients,500));
            CHECK(w.begin_celestial_transit(7,ship,chosen.body,90003));
            w.steps(4);
            CHECK(w.complete_celestial_transit(90003));
            CHECK(w.extract_planetary_site(7,ship,chosen.body,
                chosen.latitude,chosen.longitude,false,1000));
            CHECK(w.world().oasis_reserves().at(chosen.body).nutrients_kg==3000);
            const auto site_before=w.world().planetary_sites().at(key).nutrients_kg;
            const auto ship_before=w.world().life_support(ship)->nutrients_kg;
            CHECK(!w.load_planetary_shuttle(8,ship,key,
                ShuttleCargo::Nutrients,500));
            // Orbit-side loading from the real ship is legitimate; it does
            // NOT withdraw one kilogram from the remote surface. Return that
            // same cargo to the ship before requesting surface collection.
            const auto orbit_site_nutrients=
                w.world().planetary_sites().at(key).nutrients_kg;
            const auto orbit_ship_nutrients=
                w.world().life_support(ship)->nutrients_kg;
            CHECK(w.load_planetary_shuttle(7,ship,key,
                ShuttleCargo::Nutrients,500));
            CHECK(w.world().planetary_sites().at(key).nutrients_kg==
                  orbit_site_nutrients);
            CHECK(w.unload_planetary_shuttle(7,ship,key,
                ShuttleCargo::Nutrients,500));
            CHECK(w.world().life_support(ship)->nutrients_kg==
                  orbit_ship_nutrients);
            CHECK(w.launch_planetary_shuttle(7,ship,key,81006,false));
            w.steps(3);
            CHECK(w.world().planetary_shuttles().at(key).flight_phase==2);
            const auto before_failed_load=w.world().canonical_state();
            bool nutrient_failed=false;
            try {(void)w.load_planetary_shuttle(7,ship,key,
                ShuttleCargo::Nutrients,500,FailPoint::AfterSnapshotWrite);}
            catch(const std::exception&){nutrient_failed=true;}
            CHECK(nutrient_failed && w.world().canonical_state()==before_failed_load);
            const auto surface_manifest=w.world().planetary_transport_manifest(key);
            CHECK(surface_manifest && surface_manifest->valid());
            const transport::Instruction pickup{
                77001,7,surface_manifest->vehicle,
                surface_manifest->berth,surface_manifest->other_end,
                {{transport::GoodKind::Resource,
                    static_cast<std::uint8_t>(transport::ResourceGood::Nutrients),
                    500,500}}};
            CHECK(transport::serves(*surface_manifest,pickup));
            auto foreign_pickup=pickup;foreign_pickup.owner=8;
            const auto blocked_generation=w.generation();
            CHECK(!execute_shuttle_transport_instruction(
                w,7,ship,key,foreign_pickup).accepted);
            CHECK(w.generation()==blocked_generation);
            auto equipment_request=pickup;
            equipment_request.goods={{transport::GoodKind::Equipment,550,1,10}};
            CHECK(!execute_shuttle_transport_instruction(
                w,7,ship,key,equipment_request).accepted);
            CHECK(w.generation()==blocked_generation);
            const auto loaded_order=execute_shuttle_transport_instruction(
                w,7,ship,key,pickup);
            CHECK(loaded_order.accepted && loaded_order.mutated);
            CHECK(loaded_order.goods_manifest && loaded_order.goods_manifest->valid());
            CHECK(transport::contains_good(*loaded_order.goods_manifest,
                transport::GoodKind::Resource,
                static_cast<std::uint8_t>(transport::ResourceGood::Nutrients),500));
            CHECK(w.world().planetary_sites().at(key).nutrients_kg==site_before-500);
            CHECK(w.world().planetary_shuttles().at(key).nutrients_kg==500);
            // Five hundred more kg really exist on the ground, but the
            // other 500 kg plus remaining fuel/food exceed shuttle capacity.
            CHECK(!w.load_planetary_shuttle(7,ship,key,
                ShuttleCargo::Nutrients,500));
            CHECK(w.world().canonical_state().rfind((manufactured?"galaxy-kernel-v92|":"galaxy-kernel-v87|"),0)==0);
            CHECK(Simulation::from_snapshot(w.world().canonical_state())
                  .planetary_shuttles().at(key).nutrients_kg==500);
            CHECK(!w.unload_planetary_shuttle(7,ship,key,
                ShuttleCargo::Nutrients,501));
            CHECK(w.launch_planetary_shuttle(7,ship,key,81007,true));
            CHECK(!w.unload_planetary_shuttle(7,ship,key,
                ShuttleCargo::Nutrients,500)); // no unloading in transit
            w.steps(3);
            CHECK(w.world().planetary_shuttles().at(key).flight_phase==0);
            const auto orbital_manifest=w.world().planetary_transport_manifest(key);
            CHECK(orbital_manifest && orbital_manifest->valid());
            const transport::Instruction delivery{
                77002,7,orbital_manifest->vehicle,
                orbital_manifest->other_end,orbital_manifest->berth,
                {{transport::GoodKind::Resource,
                    static_cast<std::uint8_t>(transport::ResourceGood::Nutrients),
                    500,500}}};
            const auto delivered_order=execute_shuttle_transport_instruction(
                w,7,ship,key,delivery);
            CHECK(delivered_order.accepted && delivered_order.mutated);
            CHECK(delivered_order.goods_manifest && delivered_order.goods_manifest->valid());
            CHECK(!transport::contains_good(*delivered_order.goods_manifest,
                transport::GoodKind::Resource,
                static_cast<std::uint8_t>(transport::ResourceGood::Nutrients),1));
            CHECK(w.world().life_support(ship)->nutrients_kg==ship_before+500);
            CHECK(w.world().planetary_shuttles().at(key).nutrients_kg==0);
            CHECK(w.world().planetary_sites().at(key).nutrients_kg==site_before-500);
            CHECK(w.world().total_ore()==origin);
            // PLAY-13D.4F: player posts a real local contract, named pilot
            // handles two flight legs, stock moves only at physical berths.
            const auto site_prior=w.world().planetary_sites().at(key).nutrients_kg;
            const auto ship_prior=w.world().life_support(ship)->nutrients_kg;
            CHECK(!w.post_local_delivery_order(8,ship,key,
                ShuttleCargo::Nutrients,200,77003));
            const auto precontract=w.world().canonical_state();
            bool posting_failed=false;
            try{(void)w.post_local_delivery_order(7,ship,key,
                ShuttleCargo::Nutrients,200,77003,FailPoint::AfterSnapshotWrite);}
            catch(const std::exception&){posting_failed=true;}
            CHECK(posting_failed && w.world().canonical_state()==precontract);
            // Two real two-leg deliveries need four kg of on-board water.
            // Fuel is transferred from real ship stock, not minted by jobs.
            const auto fuel=w.world().planetary_shuttles().at(key).water_kg;
            if(fuel<4)CHECK(w.load_planetary_shuttle(
                7,ship,key,ShuttleCargo::Water,4-fuel));
            CHECK(w.post_local_delivery_order(7,ship,key,
                ShuttleCargo::Nutrients,200,77003));
            if(delegated){
                CHECK(!w.delegate_transport_work_order(
                    8,ship,94003,WorkTaskKind::LocalDelivery,77003));
                CHECK(!w.delegate_transport_work_order(
                    7,ship,94003,WorkTaskKind::IntercolonyFreight,77003));
                const auto before_delegation=w.world().canonical_state();
                bool failed=false;
                try{(void)w.delegate_transport_work_order(
                    7,ship,94003,WorkTaskKind::LocalDelivery,77003,0,
                    FailPoint::AfterSnapshotWrite);}
                catch(const std::exception&){failed=true;}
                CHECK(failed);
                CHECK(w.world().canonical_state()==before_delegation);
                CHECK(w.delegate_transport_work_order(
                    7,ship,94003,WorkTaskKind::LocalDelivery,77003));
                CHECK(!w.delegate_transport_work_order(
                    7,ship,94003,WorkTaskKind::LocalDelivery,77003));
                CHECK(!w.delegate_transport_work_order(
                    7,ship,94009,WorkTaskKind::LocalDelivery,77003));
                CHECK(w.world().total_ore()==origin);
                CHECK(w.world().work_directives().at(94003).status==
                      WorkDirectiveStatus::Active);
            }
            CHECK(w.post_local_delivery_order(7,ship,key,
                ShuttleCargo::Nutrients,100,77005));
            CHECK(!w.post_local_delivery_order(7,ship,key,
                ShuttleCargo::Nutrients,100,77005)); // stable ID: no replay
            CHECK(w.world().local_delivery_board().at(77003).stage==
                  LocalDeliveryStage::Posted);
            CHECK(w.world().local_delivery_board().at(77005).stage==
                  LocalDeliveryStage::Posted);
            CHECK(w.world().canonical_state().rfind(
                delegated?"galaxy-kernel-v94|":
                (manufactured?"galaxy-kernel-v92|":"galaxy-kernel-v88|"),0)==0);
            CHECK(Simulation::from_snapshot(w.world().canonical_state())
                  .canonical_state()==w.world().canonical_state());
            w.steps(3);
            // Second contract cannot claim the only shuttle while the first
            // is in progress. Both survive a canonical snapshot mid-flight.
            CHECK(w.world().local_delivery_board().at(77005).stage==
                  LocalDeliveryStage::Posted);
            const auto in_flight=w.world().canonical_state();
            CHECK(Simulation::from_snapshot(in_flight).canonical_state()==in_flight);
            if(delegated){
                const auto forged=forge_work_completion(
                    in_flight,94003,w.world().tick());
                bool denied=false;
                try{(void)Simulation::from_snapshot(forged);}
                catch(const std::exception&){denied=true;}
                CHECK(denied); // no real shuttle unloading receipt yet
            }
            w.steps(22);
            const auto completed=w.world().local_delivery_board().at(77003);
            const auto queued=w.world().local_delivery_board().at(77005);
            CHECK(completed.stage==LocalDeliveryStage::Complete);
            CHECK(queued.stage==LocalDeliveryStage::Complete);
            if(delegated){
                CHECK(w.world().work_directives().at(94003).status==
                      WorkDirectiveStatus::Fulfilled);
                CHECK(w.world().work_directives().at(94003).resolved_tick>0);
                CHECK(!w.delegate_transport_work_order(
                    7,ship,94010,WorkTaskKind::LocalDelivery,77003));
            }
            CHECK(completed.assigned_pilot==available[2]);
            CHECK(queued.assigned_pilot==available[2]);
            CHECK(w.world().planetary_sites().at(key).nutrients_kg==site_prior-300);
            CHECK(w.world().life_support(ship)->nutrients_kg==ship_prior+300);
            CHECK(w.world().planetary_shuttles().at(key).nutrients_kg==0);
            CHECK(w.world().planetary_shuttles().at(key).flight_phase==0);
            CHECK(w.world().total_ore()==origin);
            // One understocked order must not monopolize the work board.
            // Refill with real water: the next ready order uses this same
            // finite shuttle, while the 400 kg order waits for supplies.
            const auto fuel_again=w.world().planetary_shuttles().at(key).water_kg;
            if(fuel_again<2)CHECK(w.load_planetary_shuttle(
                7,ship,key,ShuttleCargo::Water,2-fuel_again));
            CHECK(w.post_local_delivery_order(7,ship,key,
                ShuttleCargo::Nutrients,400,77004));
            if(delegated)CHECK(w.delegate_transport_work_order(
                7,ship,94005,WorkTaskKind::LocalDelivery,77004));
            CHECK(w.post_local_delivery_order(7,ship,key,
                ShuttleCargo::Nutrients,100,77006));
            const auto waiting_stock=w.world().planetary_sites().at(key).nutrients_kg;
            w.steps(15);
            CHECK(w.world().local_delivery_board().at(77004).stage==
                  LocalDeliveryStage::Posted);
            CHECK(w.world().local_delivery_board().at(77006).stage==
                  LocalDeliveryStage::Complete);
            CHECK(w.world().planetary_sites().at(key).nutrients_kg==waiting_stock-100);
            CHECK(w.world().life_support(ship)->nutrients_kg==ship_prior+400);
            CHECK(!w.cancel_local_delivery_order(8,77004));
            const auto pre_cancel=w.world().canonical_state();
            bool cancel_failed=false;
            try{(void)w.cancel_local_delivery_order(
                7,77004,FailPoint::AfterSnapshotWrite);}
            catch(const std::exception&){cancel_failed=true;}
            CHECK(cancel_failed && w.world().canonical_state()==pre_cancel);
            CHECK(w.cancel_local_delivery_order(7,77004));
            CHECK(w.world().local_delivery_board().at(77004).stage==
                  LocalDeliveryStage::Cancelled);
            if(delegated){
                CHECK(w.world().work_directives().at(94005).status==
                      WorkDirectiveStatus::Cancelled);
                const auto restored=w.world().canonical_state();
                CHECK(Simulation::from_snapshot(restored).canonical_state()==restored);
            }
            CHECK(!w.cancel_local_delivery_order(7,77003));
            w.steps(12);
            CHECK(w.world().planetary_sites().at(key).nutrients_kg==waiting_stock-100);
            CHECK(w.world().life_support(ship)->nutrients_kg==ship_prior+400);
            CHECK(w.world().total_ore()==origin);
            // PLAY-13D.4G: the completed planetary pickup becomes one real
            // sealed shipment on an independently existing crewed freighter.
            const auto from_before=w.world().life_support(ship)->nutrients_kg;
            const auto to_before=w.world().life_support(receiver)->nutrients_kg;
            CHECK(!w.load_intercolony_freight(8,77003,freighter,receiver));
            CHECK(!w.load_intercolony_freight(7,77004,freighter,receiver));
            CHECK(!w.load_intercolony_freight(7,77003,ship,receiver));
            CHECK(!w.load_intercolony_freight(7,77003,freighter,ship));
            const auto before_seal=w.world().canonical_state();
            bool seal_failed=false;
            try{(void)w.load_intercolony_freight(7,77003,freighter,receiver,
                FailPoint::AfterSnapshotWrite);}
            catch(const std::exception&){seal_failed=true;}
            CHECK(seal_failed && w.world().canonical_state()==before_seal);
            CHECK(w.load_intercolony_freight(7,77003,freighter,receiver));
            CHECK(!w.load_intercolony_freight(7,77003,freighter,receiver));
            if(delegated){
                CHECK(!w.delegate_transport_work_order(
                    8,ship,94004,WorkTaskKind::IntercolonyFreight,77003,94003));
                CHECK(w.delegate_transport_work_order(
                    7,ship,94004,WorkTaskKind::IntercolonyFreight,77003,94003));
                CHECK(!w.delegate_transport_work_order(
                    7,ship,94011,WorkTaskKind::IntercolonyFreight,77003));
                CHECK(w.world().work_directives().at(94004).requires_directive==
                      94003);
            }
            CHECK(w.world().life_support(ship)->nutrients_kg==from_before-200);
            CHECK(w.world().colony_ship(freighter)->goods_kg.at("freight_nutrients")==200);
            const auto freight_manifest=w.world().intercolony_transport_manifest(77003);
            CHECK(freight_manifest && freight_manifest->valid());
            CHECK(freight_manifest->vehicle==freighter);
            CHECK(freight_manifest->mode==transport::Mode::OtherVehicle);
            CHECK(transport::contains_good(*freight_manifest,
                  transport::GoodKind::Resource,
                  static_cast<std::uint8_t>(transport::ResourceGood::Nutrients),200));
            CHECK(w.world().life_support(receiver)->nutrients_kg==to_before);
            CHECK(w.world().intercolony_freight().at(77003).stage==
                  IntercolonyFreightStage::Loaded);
            // WO5-B: loaded is proved by original physically sealed cargo,
            // not the administrative booking or a claim of remote arrival.
            const auto freight_loaded=logistics05::inspect_original_freight_evidence(
                w.world(),7,77003);
            CHECK(freight_loaded &&
                  freight_loaded->original_stage==IntercolonyFreightStage::Loaded &&
                  freight_loaded->physically_carried_kg==200 &&
                  !freight_loaded->actual_arrival_proved);
            CHECK(!logistics05::inspect_original_freight_evidence(
                w.world(),8,77003));
            CHECK(!w.deliver_intercolony_freight(7,77003)); // wrong frame
            const auto aboard=w.world().canonical_state();
            // This sealed-cargo snapshot predates navigation posting.
            CHECK(aboard.rfind(delegated?"galaxy-kernel-v94|":
                (manufactured?"galaxy-kernel-v92|":"galaxy-kernel-v89|"),0)==0);
            CHECK(Simulation::from_snapshot(aboard).canonical_state()==aboard);
            // A snapshot with sealed physical cargo but no matching order
            // is rejected; nobody can resurrect phantom freight by deleting
            // the delivery manifest while leaving its kg in the hold.
            auto orphan=aboard;
            const auto row=orphan.find("interfreight|77003|");
            CHECK(row!=std::string::npos);
            const auto row_end=orphan.find('\n',row);
            CHECK(row_end!=std::string::npos);
            orphan.erase(row,row_end-row+1);
            bool rejected_orphan=false;
            try{(void)Simulation::from_snapshot(orphan);}
            catch(const std::exception&){rejected_orphan=true;}
            CHECK(rejected_orphan);
            if(delegated){
                const auto forged=forge_work_completion(
                    aboard,94004,w.world().tick());
                bool denied=false;
                try{(void)Simulation::from_snapshot(forged);}
                catch(const std::exception&){denied=true;}
                CHECK(denied); // carrier has not physically arrived
            }
            std::uint64_t autonomous_pilot=0;
            if(autonomous){
                for(const auto& [id,n]:w.world().npc_records())
                    if(n.home_ship==freighter && n.location==freighter &&
                       n.role==NpcRole::Ship){autonomous_pilot=id;break;}
                CHECK(autonomous_pilot);
                const auto pre_flight=w.world().canonical_state();
                bool failed=false;
                try{(void)w.queue_npc_flight(7,freighter,autonomous_pilot,
                    receiver_body,95021,99021,NpcFlightPurpose::Freight,
                    FailPoint::AfterSnapshotWrite);}
                catch(const std::exception&){failed=true;}
                CHECK(failed);
                CHECK(w.world().canonical_state()==pre_flight);
                CHECK(!w.queue_npc_flight(8,freighter,autonomous_pilot,
                    receiver_body,95021,99021,NpcFlightPurpose::Freight));
                CHECK(w.queue_npc_flight(7,freighter,autonomous_pilot,
                    receiver_body,95021,99021,NpcFlightPurpose::Freight));
                CHECK(!w.world().npc_available_for_duty(autonomous_pilot));
                CHECK(!w.queue_npc_flight(7,freighter,autonomous_pilot,
                    receiver_body,95022,99022,NpcFlightPurpose::Survey));
                CHECK(!w.begin_celestial_transit(7,freighter,receiver_body,99021));
                const auto posting=w.world().canonical_state();
                CHECK(posting.rfind("galaxy-kernel-v95|",0)==0);
                CHECK(Simulation::from_snapshot(posting).canonical_state()==posting);
                // Crash at the same SQLite publication boundary as the
                // original host transit. Neither task nor voyage advances.
                const auto original_tick=w.world().tick();
                bool crashed=false;
                try{w.step(FailPoint::AfterSnapshotWrite);}
                catch(const std::exception&){crashed=true;}
                CHECK(crashed);
                CHECK(w.world().canonical_state()==posting);
                CHECK(w.world().tick()==original_tick);
                w.steps(2);
                CHECK(w.world().npc_flight_orders().at(95021).phase==
                      NpcFlightPhase::InTransit);
                CHECK(w.world().pending_celestial_transits().count(99021)==1);
                const auto in_transit=w.world().canonical_state();
                CHECK(Simulation::from_snapshot(in_transit).canonical_state()==in_transit);
                bool forged_arrival=false;
                try{(void)Simulation::from_snapshot(forge_npc_flight_arrival(
                    in_transit,95021,w.world().tick()));}
                catch(const std::exception&){forged_arrival=true;}
                CHECK(forged_arrival);
                CHECK(!w.cancel_npc_flight(7,95021));
                CHECK(!w.deliver_intercolony_freight(7,77003));
                w.steps(3);
                CHECK(w.world().npc_flight_orders().at(95021).phase==
                      NpcFlightPhase::Arrived);
                CHECK(w.world().completed_celestial_transits().count(99021)==1);
                CHECK(w.world().npc_flight_orders().at(95021).resolved_tick==
                      w.world().completed_celestial_transits().at(99021).completed_tick);
                CHECK(w.world().npc_available_for_duty(autonomous_pilot));
                const auto arrived=w.world().canonical_state();
                CHECK(Simulation::from_snapshot(arrived).canonical_state()==arrived);
            }else{
                CHECK(w.begin_celestial_transit(7,freighter,receiver_body,99021));
                CHECK(!w.deliver_intercolony_freight(7,77003)); // in flight
                w.steps(4);
                CHECK(!w.deliver_intercolony_freight(7,77003)); // no arrival receipt
                CHECK(w.complete_celestial_transit(99021));
            }
            if(rendezvous_freight){
                CHECK(autonomous && !delegated);
                CHECK(!w.queue_npc_ship_rendezvous(
                    8,freighter,autonomous_pilot,receiver,97021));
                CHECK(w.queue_npc_ship_rendezvous(7,freighter,autonomous_pilot,
                    receiver,97021,NpcFlightPurpose::Freight));
                CHECK(!ship_interaction::deliver_freight_after_rendezvous(
                    w,7,77003,97021));
                bool positioned=false;
                for(int i=0;i<300;i++){
                    w.step();
                    if(w.world().npc_flight_orders().at(97021).phase==
                       NpcFlightPhase::Arrived){positioned=true;break;}
                }
                CHECK(positioned);
                CHECK(ship_interaction::rendezvous_preflight(
                    w.world(),7,freighter,receiver,97021)==
                    ship_interaction::Readiness::Ready);
                const auto before_transfer=w.world().canonical_state();
                bool failed=false;
                try{(void)ship_interaction::deliver_freight_after_rendezvous(
                    w,7,77003,97021,FailPoint::AfterSnapshotWrite);}
                catch(const std::exception&){failed=true;}
                CHECK(failed && w.world().canonical_state()==before_transfer);
                CHECK(!ship_interaction::deliver_freight_after_rendezvous(
                    w,8,77003,97021));
                CHECK(ship_interaction::deliver_freight_after_rendezvous(
                    w,7,77003,97021));
            }else{
            const auto before_unload=w.world().canonical_state();
            bool unload_failed=false;
            try{(void)w.deliver_intercolony_freight(7,77003,
                FailPoint::AfterSnapshotWrite);}
            catch(const std::exception&){unload_failed=true;}
            CHECK(unload_failed && w.world().canonical_state()==before_unload);
            CHECK(!w.deliver_intercolony_freight(8,77003));
            if(delegated){
                // No user unload call: the coordinator must recheck the
                // original completed voyage receipt inside native world.step.
                CHECK(w.world().work_directives().at(94004).status==
                      WorkDirectiveStatus::Active);
                w.step();
                CHECK(w.world().work_directives().at(94004).status==
                      WorkDirectiveStatus::Fulfilled);
            }else CHECK(w.deliver_intercolony_freight(7,77003));
            }
            CHECK(!w.deliver_intercolony_freight(7,77003)); // no duplication
            CHECK(w.world().life_support(receiver)->nutrients_kg==to_before+200);
            CHECK(w.world().life_support(ship)->nutrients_kg==from_before-200);
            CHECK(w.world().colony_ship(freighter)->goods_kg.count("freight_nutrients")==0);
            const auto delivered_manifest=w.world().intercolony_transport_manifest(77003);
            CHECK(delivered_manifest && delivered_manifest->valid());
            CHECK(delivered_manifest->contents.empty());
            CHECK(w.world().intercolony_freight().at(77003).arrival_receipt==99021);
            CHECK(w.world().intercolony_freight().at(77003).stage==
                  IntercolonyFreightStage::Delivered);
            // WO5-B: destination was credited only after original
            // celestial receipt 99021 and the sealed cargo left the carrier.
            const auto freight_delivered=
                logistics05::inspect_original_freight_evidence(w.world(),7,77003);
            CHECK(freight_delivered &&
                  freight_delivered->original_stage==
                      IntercolonyFreightStage::Delivered &&
                  freight_delivered->actual_arrival_proved &&
                  freight_delivered->original_arrival_receipt==99021 &&
                  freight_delivered->physically_carried_kg==0);
            CHECK(w.world().total_ore()==origin);
            // Single-contract path: player assigns a REAL returning carrier
            // to an unfinished NPC surface pickup. Native ticks load the
            // carrier automatically after actual shuttle delivery, never at
            // posting time. The same carrier makes a second physical trip.
            CHECK(w.begin_celestial_transit(7,freighter,chosen.body,99022));
            w.steps(4);
            CHECK(w.complete_celestial_transit(99022));
            const auto fuel_for_next=w.world().planetary_shuttles().at(key).water_kg;
            if(fuel_for_next<2)CHECK(w.load_planetary_shuttle(
                7,ship,key,ShuttleCargo::Water,2-fuel_for_next));
            const auto before_plan=w.world().canonical_state();
            bool plan_failed=false;
            try{(void)w.post_intercolony_work_order(7,ship,key,
                ShuttleCargo::Nutrients,100,77007,freighter,receiver,
                FailPoint::AfterSnapshotWrite);}
            catch(const std::exception&){plan_failed=true;}
            CHECK(plan_failed && w.world().canonical_state()==before_plan);
            CHECK(!w.post_intercolony_work_order(8,ship,key,
                ShuttleCargo::Nutrients,100,77007,freighter,receiver));
            if(manufactured){
                CHECK(fleet_freight_eligible(w.world(),fleet,7,freighter,receiver));
                CHECK(fleet.select(w.world(),freighter));
                CHECK(!fleet_freight_eligible(w.world(),fleet,7,freighter,receiver));
                CHECK(fleet.select(w.world(),ship));
                CHECK(fleet_freight_eligible(w.world(),fleet,7,freighter,receiver));
            }
            CHECK(w.post_intercolony_work_order(7,ship,key,
                ShuttleCargo::Nutrients,100,77007,freighter,receiver));
            // WO5-B: an atomically posted pickup is still NOT a loaded
            // carrier; original freight receipts prove the distinction.
            const auto awaiting=logistics05::inspect_original_freight_evidence(
                w.world(),7,77007);
            CHECK(awaiting &&
                  awaiting->original_stage==
                      IntercolonyFreightStage::AwaitingPickup &&
                  !awaiting->actual_arrival_proved &&
                  awaiting->physically_carried_kg==0);
            if(delegated){
                // WO5-E: register a real regional scheduler in this very
                // SQLite database, not an unrelated mock regional store.
                // The original posted intercolony shipment is still
                // physically unresolved: cold/detailed promotion MUST fail.
                regional_events::CheckpointStore cp(db.string(),"wo5-native-freight");
                regional_events::Scheduler cold;
                cold_colony::State cold_home{};
                cold.add_colony("freight-home",cold_home);
                CHECK(cp.save(cold,0)==1);
                regional_events::RegionalModeAuthority authority(cp);
                const auto state=authority.enroll();
                CHECK(state.owner==regional_events::SimulationOwner::Cold &&
                      state.phase==regional_events::TransitionPhase::Stable);
                const auto audit=logistics05::observe_native_regional_recovery(
                    db.string(),w,cp,authority,7,state.generation,
                    state.checkpoint_revision);
                CHECK(audit &&
                      audit->awaiting>=1 &&
                      audit->has_unresolved_original_freight &&
                      !audit->may_consider_detailed_only_work &&
                      audit->original_world_generation==w.generation());
                bool unsafe_handoff=false;
                try{
                    (void)authority.prepare("wo5-unsafe-inflight",
                        regional_events::SimulationOwner::Detailed,
                        state.generation,state.checkpoint_revision,
                        state.boundary_tick);
                }catch(const std::exception&){unsafe_handoff=true;}
                CHECK(unsafe_handoff &&
                      authority.state().phase==
                          regional_events::TransitionPhase::Stable);
                // Even a perfectly matching forged counter in an unrelated
                // SQLite file cannot borrow this world's physical cargo.
                const auto foreign_db=db.string()+".wo5_other.sqlite";
                regional_events::CheckpointStore unrelated(foreign_db,
                    "wo5-native-freight");
                CHECK(unrelated.save(cold,0)==1);
                regional_events::RegionalModeAuthority foreign(unrelated);
                const auto fake=foreign.enroll();
                CHECK(!logistics05::observe_native_regional_recovery(
                    db.string(),w,unrelated,foreign,7,fake.generation,
                    fake.checkpoint_revision));
            }
            if(delegated){
                // WO5-D: one owner-authorized contract over the actual
                // original posted work order, 2 real finite vehicles and
                // an existing physical NPC/pilot roster. No shadow jobs.
                const logistics05::DelegationPolicy policy{true,true,true};
                CHECK(!logistics05::inspect_native_contract(
                    w.world(),8,ship,77007,94007,94008,policy));
                CHECK(!logistics05::inspect_native_contract(
                    w.world(),7,ship,77007,94007,94008,
                    logistics05::DelegationPolicy{}));
                const auto proposal=logistics05::inspect_native_contract(
                    w.world(),7,ship,77007,94007,94008,policy);
                CHECK(proposal &&
                      proposal->real_carrier==freighter &&
                      proposal->real_receiver==receiver &&
                      proposal->kilograms==100 &&
                      proposal->eligible_native_pilots+
                          (proposal->currently_assigned_pilot?1:0)>0 &&
                      !proposal->pickup_already_delegated &&
                      !proposal->freight_already_delegated);
                const auto prior_hash=w.world().state_hash();
                const auto prior_mass=w.world().total_ore();
                CHECK(logistics05::commit_native_contract(
                    w,*proposal,false).status==
                    logistics05::DelegationResult::Refused);
                CHECK(w.world().state_hash()==prior_hash);
                bool second_rolled_back=false;
                try{
                    (void)logistics05::commit_native_contract(
                        w,*proposal,true,FailPoint::AfterSnapshotWrite);
                }catch(const std::exception&){second_rolled_back=true;}
                CHECK(second_rolled_back);
                // First durable original directive SURVIVES second-step
                // SQLite failure; second does not, and no cargo moves.
                CHECK(w.world().work_directives().count(94007)==1 &&
                      w.world().work_directives().count(94008)==0 &&
                      w.world().total_ore()==prior_mass);
                const auto recovery_offer=logistics05::inspect_native_contract(
                    w.world(),7,ship,77007,94007,94008,policy);
                CHECK(recovery_offer &&
                      recovery_offer->pickup_already_delegated &&
                      !recovery_offer->freight_already_delegated);
                const auto restarted_generation=w.generation();
                const auto finish=logistics05::commit_native_contract(
                    w,*recovery_offer,true);
                CHECK(finish.status==
                    logistics05::DelegationResult::AllOriginalDirectivesCommitted &&
                      w.generation()==restarted_generation+1 &&
                      w.world().work_directives().at(94008).requires_directive==94007);
                CHECK(logistics05::commit_native_contract(
                    w,*proposal,true).status==
                    logistics05::DelegationResult::Refused);
                const auto after=logistics05::inspect_native_contract_status(
                    w.world(),7,77007,94007,94008);
                CHECK(after &&
                      after->pickup_status==WorkDirectiveStatus::Active &&
                      after->freight_status==WorkDirectiveStatus::Active &&
                      !after->real_delivery_confirmed);
                // Independent SQLite recovery and owner revocation test:
                // cancel only FUTURE NPC delegation, not the real finite
                // pickup, in-transit material or its original freight leg.
                const auto cancelled_db=db.string()+".wo5_cancel_delegation.sqlite";
                PersistentWorld::recover_copy(db.string(),cancelled_db);
                {
                    PersistentWorld copy(cancelled_db,Simulation(99437));
                    CHECK(copy.world().work_directives().count(94008)==1);
                    const auto untouched=copy.world().state_hash();
                    bool cancelled_fault=false;
                    try{
                        (void)logistics05::revoke_native_delegation(
                            copy,7,94007,94008,true,
                            FailPoint::AfterSnapshotWrite);
                    }catch(const std::exception&){cancelled_fault=true;}
                    CHECK(cancelled_fault &&
                          copy.world().state_hash()==untouched);
                    CHECK(!logistics05::revoke_native_delegation(
                        copy,8,94007,94008,true));
                    CHECK(logistics05::revoke_native_delegation(
                        copy,7,94007,94008,true));
                    CHECK(copy.world().work_directives().at(94007).status==
                          WorkDirectiveStatus::Cancelled &&
                          copy.world().work_directives().at(94008).status==
                          WorkDirectiveStatus::Cancelled &&
                          copy.world().local_delivery_board().at(77007).stage==
                          LocalDeliveryStage::Posted &&
                          copy.world().intercolony_freight().at(77007).stage==
                          IntercolonyFreightStage::AwaitingPickup &&
                          copy.world().total_ore()==prior_mass);
                    CHECK(!logistics05::revoke_native_delegation(
                        copy,7,94007,94008,true));
                }
                {
                    PersistentWorld restarted(cancelled_db,Simulation(99438));
                    CHECK(restarted.world().work_directives().at(94008).status==
                          WorkDirectiveStatus::Cancelled &&
                          restarted.world().local_delivery_board().count(77007)==1);
                }
                CHECK(w.world().universal_work_board(7,ship).size()>0);
            }
            if(manufactured){
                // The durable work order reserves exactly this hull. A
                // second job cannot seize it or synthesize inventory.
                CHECK(!fleet_freight_eligible(w.world(),fleet,7,freighter,receiver));
                CHECK(fleet.piloted()==ship);
                CHECK(w.world().freighter_build_orders().at(92001).source_ship==ship);
            }
            CHECK(w.world().intercolony_freight().at(77007).stage==
                  IntercolonyFreightStage::AwaitingPickup);
            CHECK(w.world().colony_ship(freighter)->goods_kg.count("freight_nutrients")==0);
            w.steps(22);
            CHECK(w.world().local_delivery_board().at(77007).stage==
                  LocalDeliveryStage::Complete);
            CHECK(w.world().intercolony_freight().at(77007).stage==
                  IntercolonyFreightStage::Loaded);
            CHECK(w.world().colony_ship(freighter)->goods_kg.at("freight_nutrients")==100);
            if(delegated){
                CHECK(w.world().work_directives().at(94007).status==
                      WorkDirectiveStatus::Fulfilled);
                CHECK(w.world().work_directives().at(94008).status==
                      WorkDirectiveStatus::Active);
                const auto in_flight=logistics05::inspect_native_contract_status(
                    w.world(),7,77007,94007,94008);
                CHECK(in_flight &&
                      in_flight->native_pickup==LocalDeliveryStage::Complete &&
                      in_flight->native_freight==IntercolonyFreightStage::Loaded &&
                      !in_flight->real_delivery_confirmed &&
                      in_flight->original_pilot!=0);
                // WO5-E second physical boundary: the shuttle finished
                // its journey, but the SEALED 100kg are still on the
                // freighter and must not be transformed into cold macro
                // inventory while crossing a regional owner epoch.
                regional_events::CheckpointStore cp(
                    db.string(),"wo5-native-freight");
                regional_events::RegionalModeAuthority authority(cp);
                const auto token=authority.state();
                const auto loaded_audit=logistics05::observe_native_regional_recovery(
                    db.string(),w,cp,authority,7,token.generation,
                    token.checkpoint_revision);
                CHECK(loaded_audit && loaded_audit->loaded==1 &&
                      loaded_audit->awaiting==0 &&
                      loaded_audit->delivered>=1 &&
                      loaded_audit->has_unresolved_original_freight);
                // Native carrier reused after a PRIOR completed nutrient
                // delivery: its new 100kg belong solely to 77007. The
                // old 77003 delivery receipt remains independently valid.
                const auto older=logistics05::inspect_original_freight_evidence(
                    w.world(),7,77003);
                CHECK(older && older->actual_arrival_proved &&
                      older->physically_carried_kg==0 &&
                      older->other_loaded_same_good_kg==100);
                bool cannot_cutover=false;
                try{
                    (void)authority.prepare("wo5-loaded-cutover",
                        regional_events::SimulationOwner::Detailed,
                        token.generation,token.checkpoint_revision,
                        token.boundary_tick);
                }catch(const std::exception&){cannot_cutover=true;}
                CHECK(cannot_cutover &&
                      authority.state().phase==
                          regional_events::TransitionPhase::Stable);
            }
            if(delegated){
                // WO5-D: the original named-NPC navigation service moves
                // precisely the REAL 100kg carrier already sealed by the
                // original local freight operation. No imagined carrier
                // itinerary or free arrival; original native receipt wins.
                const auto navigation=logistics05::inspect_native_npc_freight_flight(
                    w.world(),7,77007,95023,99023);
                CHECK(navigation && navigation->carrier_ship==freighter &&
                      navigation->destination_ship==receiver &&
                      navigation->destination_body==receiver_body &&
                      navigation->native_pilot_id!=0);
                CHECK(!logistics05::inspect_native_npc_freight_flight(
                    w.world(),8,77007,95123,99123));
                const auto flight_hash=w.world().state_hash();
                CHECK(!logistics05::commit_native_npc_freight_flight(
                    w,*navigation,false));
                CHECK(w.world().state_hash()==flight_hash);
                bool flight_interrupted=false;
                try{
                    (void)logistics05::commit_native_npc_freight_flight(
                        w,*navigation,true,FailPoint::AfterSnapshotWrite);
                }catch(const std::exception&){flight_interrupted=true;}
                CHECK(flight_interrupted &&
                      w.world().state_hash()==flight_hash);
                CHECK(logistics05::commit_native_npc_freight_flight(
                    w,*navigation,true));
                CHECK(!logistics05::commit_native_npc_freight_flight(
                    w,*navigation,true)); // never schedule second copy
                w.steps(5);
                CHECK(w.world().npc_flight_orders().at(95023).phase==
                      NpcFlightPhase::Arrived &&
                      w.world().npc_flight_orders().at(95023).pilot_npc==
                          navigation->native_pilot_id);
                CHECK(w.world().completed_celestial_transits().count(99023)==1);
            }else if(autonomous){
                CHECK(w.queue_npc_flight(7,freighter,autonomous_pilot,
                    receiver_body,95023,99023,NpcFlightPurpose::Freight));
                w.steps(5);
                CHECK(w.world().npc_flight_orders().at(95023).phase==
                      NpcFlightPhase::Arrived);
                CHECK(w.world().completed_celestial_transits().count(99023)==1);
            }else{
                CHECK(w.begin_celestial_transit(7,freighter,receiver_body,99023));
                w.steps(4);
                CHECK(w.complete_celestial_transit(99023));
            }
            if(delegated){
                w.step();
                CHECK(w.world().work_directives().at(94008).status==
                      WorkDirectiveStatus::Fulfilled);
                const auto delivered=logistics05::inspect_native_contract_status(
                    w.world(),7,77007,94007,94008);
                CHECK(delivered &&
                      delivered->pickup_status==WorkDirectiveStatus::Fulfilled &&
                      delivered->freight_status==WorkDirectiveStatus::Fulfilled &&
                      delivered->native_freight==IntercolonyFreightStage::Delivered &&
                      delivered->original_arrival_receipt==99023 &&
                      delivered->real_delivery_confirmed);
                CHECK(!logistics05::inspect_native_contract_status(
                    w.world(),8,77007,94007,94008));
                // WO5-E: the exact ORIGINAL delivery, not a planning
                // receipt or a cold simulation, releases the handoff fence.
                // Recreate the regional handle after the fault/restart
                // and enforce old-epoch refusal across both directions.
                regional_events::CheckpointStore cp(
                    db.string(),"wo5-native-freight");
                regional_events::RegionalModeAuthority authority(cp);
                const auto cold_state=authority.state();
                const auto safe=logistics05::observe_native_regional_recovery(
                    db.string(),w,cp,authority,7,cold_state.generation,
                    cold_state.checkpoint_revision);
                CHECK(safe && safe->awaiting==0 && safe->loaded==0 &&
                      safe->delivered>=2 &&
                      !safe->has_unresolved_original_freight &&
                      !safe->may_consider_detailed_only_work);
                const auto prior_world_hash=w.world().state_hash();
                const auto prior_world_generation=w.generation();
                const auto promote=authority.prepare(
                    "wo5-native-to-detailed",
                    regional_events::SimulationOwner::Detailed,
                    cold_state.generation,cold_state.checkpoint_revision,
                    cold_state.boundary_tick);
                const auto during=logistics05::observe_native_regional_recovery(
                    db.string(),w,cp,authority,7,promote.generation,
                    promote.checkpoint_revision);
                CHECK(during && during->phase==
                      regional_events::TransitionPhase::ToDetailed &&
                      !during->may_consider_detailed_only_work);
                bool transient=false;
                try{
                    (void)authority.commit(
                        "wo5-native-to-detailed",promote.generation,
                        promote.checkpoint_revision,
                        [](sqlite3*,regional_events::Scheduler&){
                            throw std::runtime_error("injected handoff stage crash");
                        });
                }catch(const std::exception&){transient=true;}
                CHECK(transient && authority.state().phase==
                      regional_events::TransitionPhase::ToDetailed &&
                      w.world().state_hash()==prior_world_hash &&
                      w.generation()==prior_world_generation);
                const auto warm=authority.commit(
                    "wo5-native-to-detailed",promote.generation,
                    promote.checkpoint_revision,
                    [](sqlite3*,regional_events::Scheduler&){});
                CHECK(warm.owner==regional_events::SimulationOwner::Detailed &&
                      warm.phase==regional_events::TransitionPhase::Stable &&
                      warm.generation>cold_state.generation &&
                      w.generation()==prior_world_generation);
                CHECK(!logistics05::observe_native_regional_recovery(
                    db.string(),w,cp,authority,7,cold_state.generation,
                    cold_state.checkpoint_revision));
                const auto warm_audit=logistics05::observe_native_regional_recovery(
                    db.string(),w,cp,authority,7,warm.generation,
                    warm.checkpoint_revision);
                CHECK(warm_audit && warm_audit->may_consider_detailed_only_work &&
                      warm_audit->delivered>=2 &&
                      !warm_audit->has_unresolved_original_freight);
                const auto demote=authority.prepare(
                    "wo5-native-to-cold",
                    regional_events::SimulationOwner::Cold,
                    warm.generation,warm.checkpoint_revision,warm.boundary_tick);
                const auto cold_again=authority.commit(
                    "wo5-native-to-cold",demote.generation,
                    demote.checkpoint_revision,
                    [](sqlite3*,regional_events::Scheduler&){});
                const auto audited=logistics05::observe_native_regional_recovery(
                    db.string(),w,cp,authority,7,cold_again.generation,
                    cold_again.checkpoint_revision);
                CHECK(audited && !audited->has_unresolved_original_freight &&
                      !audited->may_consider_detailed_only_work &&
                      audited->delivered>=2 &&
                      w.world().state_hash()==prior_world_hash &&
                      w.generation()==prior_world_generation);
                bool replay_rejected=false;
                try{
                    (void)authority.commit("wo5-native-to-cold",
                        demote.generation,demote.checkpoint_revision,
                        [](sqlite3*,regional_events::Scheduler&){});
                }catch(const std::exception&){replay_rejected=true;}
                CHECK(replay_rejected);
                // Independent DB copy is a DIFFERENT physical owner
                // instance, yet must contain the SAME committed regional
                // epoch, original twice-delivered cargo and unique receipts.
                const auto recovered_path=db.string()+".wo5-regional-recovered";
                PersistentWorld::recover_copy(db.string(),recovered_path);
                {
                    PersistentWorld restored(recovered_path,Simulation(613));
                    regional_events::CheckpointStore restored_checkpoint(
                        recovered_path,"wo5-native-freight");
                    regional_events::RegionalModeAuthority restored_owner(
                        restored_checkpoint);
                    const auto epoch=restored_owner.state();
                    const auto restored_audit=
                        logistics05::observe_native_regional_recovery(
                            recovered_path,restored,restored_checkpoint,
                            restored_owner,7,epoch.generation,
                            epoch.checkpoint_revision);
                    CHECK(epoch.owner==regional_events::SimulationOwner::Cold &&
                          epoch.phase==regional_events::TransitionPhase::Stable &&
                          epoch.generation==cold_again.generation &&
                          restored.world().canonical_state()==
                              w.world().canonical_state() &&
                          restored_audit && restored_audit->loaded==0 &&
                          restored_audit->awaiting==0 &&
                          restored_audit->delivered>=2 &&
                          !restored_audit->may_consider_detailed_only_work);
                }
            }else CHECK(w.deliver_intercolony_freight(7,77007));
            CHECK(w.world().intercolony_freight().at(77007).arrival_receipt==99023);
            CHECK(w.world().life_support(receiver)->nutrients_kg==to_before+300);
            CHECK(w.world().total_ore()==origin);
            if(manufactured){
                // The same minted hull is also an ordinary piloted ship:
                // actual owner-issued yaw and thrust must commit. Neither
                // command is routed through the founding colony's actor ID.
                const auto prior_heading=w.world().find(freighter)->heading_deg;
                Command turn{};
                turn.id=99024;turn.sequence=turn.id;
                turn.execute_tick=w.world().tick()+1;
                turn.issuer=7;turn.actor=freighter;
                turn.action=Action::Thrust;turn.amount=15;
                CHECK(w.submit(turn));
                w.step();
                CHECK(w.world().events().back().result==Outcome::Applied);
                CHECK(w.world().find(freighter)->heading_deg==
                      (prior_heading+15)%360);
                const auto before_move=*w.world().find(freighter);
                Command thrust{};
                thrust.id=99025;thrust.sequence=thrust.id;
                thrust.execute_tick=w.world().tick()+1;
                thrust.issuer=7;thrust.actor=freighter;
                thrust.action=Action::Thrust;thrust.thrust_x=1;
                CHECK(w.submit(thrust));
                w.step();
                CHECK(w.world().events().back().result==Outcome::Applied);
                const auto after_move=*w.world().find(freighter);
                CHECK(after_move.velocity.x!=before_move.velocity.x ||
                      after_move.velocity.y!=before_move.velocity.y ||
                      after_move.position.x!=before_move.position.x ||
                      after_move.position.y!=before_move.position.y);
                CHECK(w.world().total_ore()==origin);
            }
            if(autonomous){
                // Mission-neutral proof: the SAME navigation engine can fly
                // the original receiver ship on a survey/rescue-purpose
                // return, without any freight plan, cargo or new NPC.
                std::uint64_t receiver_pilot=0;
                for(const auto& [id,n]:w.world().npc_records())
                    if(n.home_ship==receiver && n.location==receiver &&
                       n.role==NpcRole::Ship){receiver_pilot=id;break;}
                CHECK(receiver_pilot);
                CHECK(w.queue_npc_flight(7,receiver,0,
                    chosen.body,95100,99100,NpcFlightPurpose::Survey));
                CHECK(w.world().npc_flight_orders().at(95100).pilot_npc==
                      receiver_pilot);
                CHECK(!w.cancel_npc_flight(8,95100));
                CHECK(w.cancel_npc_flight(7,95100));
                CHECK(w.world().npc_flight_orders().at(95100).phase==
                      NpcFlightPhase::Cancelled);
                CHECK(w.queue_npc_flight(7,receiver,receiver_pilot,
                    chosen.body,95101,99101,NpcFlightPurpose::Rescue));
                w.steps(5);
                CHECK(w.world().npc_flight_orders().at(95101).phase==
                      NpcFlightPhase::Arrived);
                CHECK(w.world().completed_celestial_transits().count(99101)==1);
                CHECK(w.world().ship_frames().at(receiver).anchor_body==chosen.body);
                CHECK(w.world().npc_flight_orders().at(95100).phase==
                      NpcFlightPhase::Cancelled);
                // The purpose tag never launched a rescue, moved occupants,
                // changed original finite cargo or changed worker identity.
                CHECK(w.world().colony_ship(receiver)->crew==3);
                CHECK(w.world().total_ore()==origin);
            }
            verified_ascent_receipt=w.world().planetary_shuttles().at(key).ascent_receipt;
            CHECK(verified_ascent_receipt!=0);
            durable=w.world().canonical_state();
            const auto expected_snapshot_header=(autonomous||delegated)?"galaxy-kernel-v95|":
                (manufactured?"galaxy-kernel-v92|":"galaxy-kernel-v89|");
            if(durable.rfind(expected_snapshot_header,0)!=0)
                throw std::runtime_error("unexpected physical freight snapshot version: expected "+
                    std::string(expected_snapshot_header)+" actual "+
                    durable.substr(0,durable.find('|')));
            CHECK(Simulation::from_snapshot(durable).canonical_state()==durable);
        }
    }
    {
        PersistentWorld w(db.string(),Simulation(2));
        CHECK(w.world().canonical_state()==durable);
        CHECK(w.world().total_ore()==origin);
        CHECK(w.world().planetary_sites().size()==1);
        // Verify the exact original receipt captured after the real flights
        // and work-board operations survives independent SQLite reopening.
        // The final receipt is scenario-dependent: player ascent, named
        // pilot work order, and manufactured cargo use different IDs.
        CHECK(w.world().planetary_shuttles().at(
            PlanetarySiteKey{chosen.body,chosen.latitude,chosen.longitude,1})
              .ascent_receipt==verified_ascent_receipt);
        CHECK(!w.launch_planetary_shuttle(7,ship,
            PlanetarySiteKey{chosen.body,chosen.latitude,chosen.longitude,1},
            81001,false));
        CHECK(!w.retrieve_planetary_site(8,ship,chosen.body,chosen.latitude,chosen.longitude,false,1));
        // No material or facility exists at an arbitrary neighboring tile.
        CHECK(!w.supply_planetary_site(7,ship,chosen.body,
            chosen.latitude,std::uint16_t((chosen.longitude+1)%72),true,1));
        CHECK(!w.support_planetary_crew(7,ship,chosen.body,
            chosen.latitude,chosen.longitude,true,-1));
        CHECK(w.world().planetary_spaceport_operational(
            chosen.body,chosen.latitude,chosen.longitude));
        CHECK(w.world().planetary_sites().at(
            PlanetarySiteKey{chosen.body,chosen.latitude,chosen.longitude,1})
              .delegated_npcs[0]==first_npc);
        if(!farming) {
            CHECK(w.world().planetary_shuttles().at(
                PlanetarySiteKey{chosen.body,chosen.latitude,chosen.longitude,1})
                .nutrients_kg==0);
            CHECK(w.world().oasis_reserves().at(chosen.body).nutrients_kg==3000);
            CHECK(w.world().local_delivery_board().at(77003).stage==
                  LocalDeliveryStage::Complete);
            CHECK(w.world().local_delivery_board().at(77005).stage==
                  LocalDeliveryStage::Complete);
            CHECK(w.world().local_delivery_board().at(77006).stage==
                  LocalDeliveryStage::Complete);
            CHECK(w.world().local_delivery_board().at(77004).stage==
                  LocalDeliveryStage::Cancelled);
            CHECK(w.world().intercolony_freight().at(77003).stage==
                  IntercolonyFreightStage::Delivered);
            CHECK(w.world().intercolony_freight().at(77003).arrival_receipt==99021);
            CHECK(w.world().intercolony_freight().at(77007).stage==
                  IntercolonyFreightStage::Delivered);
            CHECK(w.world().intercolony_freight().at(77007).arrival_receipt==99023);
            if(delegated){
                CHECK(w.world().work_directives().at(94003).status==
                      WorkDirectiveStatus::Fulfilled);
                CHECK(w.world().work_directives().at(94004).status==
                      WorkDirectiveStatus::Fulfilled);
                CHECK(w.world().work_directives().at(94007).status==
                      WorkDirectiveStatus::Fulfilled);
                CHECK(w.world().work_directives().at(94008).status==
                      WorkDirectiveStatus::Fulfilled);
                CHECK(!w.delegate_transport_work_order(
                    7,ship,94015,WorkTaskKind::IntercolonyFreight,77007));
            }
            CHECK(w.world().life_support(receiver)->nutrients_kg>=1200);
            // The manufactured ship has an actual 2-person life support
            // budget throughout long planetary production and two voyages.
            // Its transferred reserves, rather than free fuel/food, support
            // the crew; no shortages may be hidden behind successful cargo.
            if(manufactured) {
                CHECK(w.world().colony_ship(freighter)->crew==2);
                CHECK(w.world().life_support(freighter)->shortages==0);
                CHECK(w.world().freighter_build_orders().at(92001).commissioned());
                CHECK(w.world().find(freighter)->manufactured_order_id==92001);
            }
            CHECK(!w.deliver_intercolony_freight(7,77003));
            CHECK(!w.post_local_delivery_order(7,ship,
                PlanetarySiteKey{chosen.body,chosen.latitude,chosen.longitude,1},
                ShuttleCargo::Nutrients,100,77003));
        }
    }
}
int main(int argc,char** argv){
    const bool manufactured_only=argc==2 && std::string(argv[1])=="--manufactured-only";
    const bool delegated_only=argc==2 && std::string(argv[1])=="--delegated-work";
    const bool nav_only=argc==2 && std::string(argv[1])=="--npc-autonomous-nav";
    const bool rendezvous_only=argc==2 && std::string(argv[1])=="--flight03-rendezvous-freight";
    if(argc>2 || (argc==2 && !manufactured_only && !delegated_only && !nav_only && !rendezvous_only)){
        std::cerr<<"Unknown test selection: --manufactured-only, --delegated-work or --npc-autonomous-nav\n";
        return 2;
    }
    const auto dir=fs::temp_directory_path()/
      ("sg_play13d3_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try{
        fs::create_directories(dir);
        if(rendezvous_only){
            scenario(dir/"rendezvous-freight.sqlite",false,false,false,true,true);
        }else if(nav_only){
            scenario(dir/"npc-nav.sqlite",false,false,true,true);
        }else if(delegated_only){
            scenario(dir/"delegated-transport.sqlite",false,false,true);
        }else if(!manufactured_only){
            scenario(dir/"farm.sqlite",true);
            scenario(dir/"extract.sqlite",false);
        }else{
            scenario(dir/"manufactured-freight.sqlite",false,true);
        }
        fs::remove_all(dir);
        std::cout<<(rendezvous_only?
            "PASS FLIGHT-04B: moving-ship rendezvous, sealed physical freight, authorized original transfer and SQLite recovery\n":
            nav_only?
            "PASS FLIGHT-01: generic NPC flight engine flies sealed freight and survey/rescue-purpose hull, original transit arrival, owner consent, crash and restart\n":
            delegated_only?
            "PASS WORK-01C: original NPC shuttle pickups and two freight voyages, coordinator physical unloading, SQLite failpoint and restart\n":
            manufactured_only?
            "PASS SHIP-01C: physically manufactured, commissioned freighter completes Planet A -> Colony B cargo delivery, SQLite restart\n":
            "PASS PLAY13D3 regional farm and finite extraction, physical custody, mass, rollback, SQLite restart\n");
    }catch(const std::exception& e){
        std::error_code ec;fs::remove_all(dir,ec);
        std::cerr<<"FAIL PLAY13D3 "<<e.what()<<'\n';
        return 1;
    }
}
