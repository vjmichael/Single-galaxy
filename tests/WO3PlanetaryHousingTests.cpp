#include "galaxy/WO3ColonyOperations.hpp"
#include "galaxy/Persistence.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error( \
    "WO3-B original native line "+std::to_string(__LINE__)+": " #x);}while(0)
struct Choice {std::uint32_t system;std::uint64_t body;std::uint16_t lat,lon;};
static Choice choose() {
    // Same real astronomical catalogue as PLAY13D's native source-host
    // acceptance. No fake or manually invented planetary surface fixture.
    for(std::uint32_t sys=1;sys<=2000;++sys) {
        Simulation p(613,sys);
        const auto ship=p.add_colony_ship(7,{0,0},60,8);
        p.provision_starter_exploration_scanner(ship);
        p.ensure_physical_celestial_systems();
        for(const auto& b:p.physical_celestial_systems().at(sys)) {
            if(b.kind!=CelestialKind::Planet && b.kind!=CelestialKind::Moon)
                continue;
            for(const auto [latitude,longitude]:std::vector<std::pair<unsigned,unsigned>>{
                {17,36},{17,18},{18,46},{16,60},{14,4},{20,32},{10,27},
                {26,52},{7,15},{28,8},{12,40},{24,55},{19,50},{15,64}}) {
                const auto region=p.scan_planetary_geography(
                    7,ship,b.id,std::uint16_t(latitude),std::uint16_t(longitude));
                if(region && !region->landscape.region.surface_water)
                    return {sys,b.id,std::uint16_t(latitude),
                            std::uint16_t(longitude)};
            }
        }
    }
    throw std::runtime_error("no legitimate buildable astronomical cell");
}
static void action(PersistentWorld& w,EntityId ship,std::uint64_t id,
                   Action kind,EntityId target=0,std::int64_t amount=1) {
    Command c{};c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
    c.issuer=7;c.actor=ship;c.target=target;c.action=kind;c.amount=amount;
    CHECK(w.submit(c));w.step();
    CHECK(w.world().events().back().command_id==id &&
          w.world().events().back().result==Outcome::Applied);
}
int main() {
 try {
    const auto choice=choose();
    const PlanetarySiteKey key{choice.body,choice.lat,choice.lon,1};
    const auto dir=fs::temp_directory_path()/
        ("wo3_surface_"+std::to_string(std::chrono::steady_clock::now().
                                       time_since_epoch().count()));
    fs::create_directories(dir);
    struct Cleanup {fs::path path;~Cleanup() {
        std::error_code ec;fs::remove_all(path,ec);
    }} cleanup{dir};
    const auto db=(dir/"original.sqlite").string();
    Simulation seed(613,choice.system);
    const auto ship=seed.add_colony_ship(7,{0,0},60,8);
    seed.provision_colony_genesis(ship,2000,2000,2000,2000,2000);
    seed.enable_individual_workforce();
    seed.provision_starter_exploration_scanner(ship);
    const auto rock=seed.add_asteroid({2000,0},60);
    seed.ensure_physical_celestial_systems();
    seed.ensure_ship_frame_registry();
    const auto initial_mass=seed.total_ore();
    PersistentWorld w(db,std::move(seed));
    action(w,ship,1001,Action::Mine,rock,30);
    action(w,ship,1002,Action::RefineOre,0,30);
    action(w,ship,1003,Action::FabricateModule,0,8);
    CHECK(w.begin_celestial_transit(7,ship,choice.body,90001));
    w.steps(4);
    CHECK(w.complete_celestial_transit(90001));
    std::vector<std::uint64_t> roster;
    for(const auto& [id,n]:w.world().npc_records())
        if(n.home_ship==ship && n.location==ship && n.role==NpcRole::Ship)
            roster.push_back(id);
    CHECK(roster.size()>=4);
    CHECK(w.build_planetary_site(7,ship,choice.body,
        choice.lat,choice.lon,roster[0],roster[1]));
    w.steps(4);
    CHECK(w.support_planetary_crew(7,ship,choice.body,choice.lat,choice.lon,
                                 false,10));
    CHECK(w.support_planetary_crew(7,ship,choice.body,choice.lat,choice.lon,
                                 true,10));
    w.steps(240);
    CHECK(w.world().planetary_spaceport_operational(choice.body,
                                                   choice.lat,choice.lon));
    // WO3-E: reconstruct a historical v86 source *without modern housing*.
    // Existing original NPC identity #0 already holds the real surface job.
    // A legacy legal address is not a new arrival or an occupied modern bed.
    {
        const auto original=w.world().canonical_state();
        const auto header_end=original.find('|');
        CHECK(header_end!=std::string::npos);
        const auto version=std::stoull(original.substr(
            std::string("galaxy-kernel-v").size(),
            header_end-std::string("galaxy-kernel-v").size()));
        CHECK(version>=78 && version<109);
        std::istringstream input(original);
        std::ostringstream old_state;
        std::string line;
        while(std::getline(input,line)){
            if(line.rfind("galaxy-kernel-v",0)==0)
                line.replace(0,header_end,"galaxy-kernel-v86");
            else if(line.rfind("npc|",0)==0){
                const auto next=line.find('|',4);
                CHECK(next!=std::string::npos);
                const auto id=std::stoull(line.substr(4,next-4));
                if(version>=86) {
                    const auto last=line.rfind('|');
                    CHECK(last!=std::string::npos);
                    const auto prior=line.rfind('|',last-1);
                    CHECK(prior!=std::string::npos);
                    line.erase(prior);
                }
                line+="|"+std::to_string(id==roster[0]?3:1)+"|"+
                    std::to_string(id==roster[0]?
                        w.world().planetary_sites().at(key).surface_location:ship);
            }
            old_state<<line<<'\n';
        }
        const auto restored_old=Simulation::from_snapshot(old_state.str());
        CHECK(restored_old.canonical_state()==old_state.str());
        const auto historical_resident=restored_old.npc_records().at(roster[0]);
        CHECK(historical_resident.legal_residence()==
              (transport::Endpoint{transport::LocationKind::PlanetarySite,
                  w.world().planetary_sites().at(key).surface_location}));
        const auto history_db=(dir/"legacy_v86.sqlite").string();
        PersistentWorld history(history_db,restored_old);
        const auto historical_hash=history.world().state_hash();
        CHECK(!history.host_import_legacy_planetary_residences(
            7,86501));
        CHECK(history.world().state_hash()==historical_hash);
        bool failed_write=false;
        try {
            (void)history.host_import_legacy_planetary_residences(
                Simulation::kServerAuthority,86501,
                FailPoint::AfterSnapshotWrite);
        }catch(const std::exception&){failed_write=true;}
        CHECK(failed_write && history.world().state_hash()==historical_hash);
        CHECK(history.host_import_legacy_planetary_residences(
            Simulation::kServerAuthority,86501));
        CHECK(!history.host_import_legacy_planetary_residences(
            Simulation::kServerAuthority,86501));
        const auto& migrated=history.world();
        CHECK(migrated.canonical_state().rfind("galaxy-kernel-v115|",0)==0);
        const auto& imported=migrated.legacy_planetary_residence_import();
        CHECK(imported && imported->source_version==86 &&
              imported->source_hash==historical_hash &&
              imported->host_request_id==86501 &&
              imported->legal_addresses.size()==1 &&
              imported->legal_addresses.at(roster[0])==
                  w.world().planetary_sites().at(key).surface_location);
        CHECK(migrated.total_ore()==initial_mass);
        const auto census=colony03::audit_planetary_surface(
            migrated,7,ship,key);
        CHECK(census && !census->permanent_housing_installed &&
              census->unused_housing_capacity==0);
        CHECK(Simulation::from_snapshot(migrated.canonical_state()).
              state_hash()==migrated.state_hash());
        auto forged=migrated.canonical_state();
        const auto field=forged.find("planetlegacyimport|");
        CHECK(field!=std::string::npos);
        const auto request=forged.find("|86501|",field);
        CHECK(request!=std::string::npos);
        forged.replace(request,7,"|0|");
        bool denied=false;
        try{(void)Simulation::from_snapshot(forged);}
        catch(const std::exception&){denied=true;}
        CHECK(denied);
        const auto historical_copy=(dir/"legacy_copy.sqlite").string();
        PersistentWorld::recover_copy(history_db,historical_copy);
        PersistentWorld recovered(historical_copy,Simulation(613));
        CHECK(recovered.world().state_hash()==migrated.state_hash());
        CHECK(recovered.world().legacy_planetary_residence_import().
              has_value());
        CHECK(recovered.world().total_ore()==initial_mass);
    }
    CHECK(!w.start_planetary_housing(7,ship,key,81200));
    const auto before_house=colony03::assess_surface_supplies(
        w.world(),7,ship,key);
    CHECK(before_house.gate==colony03::SurfaceSupplyGate::NotYetHoused);
    CHECK(!before_house.independently_sustainable);
    CHECK(w.build_planetary_shuttle(7,ship,key));
    CHECK(w.assign_planetary_shuttle_pilot(7,ship,key,roster[2]));
    CHECK(w.load_planetary_shuttle(7,ship,key,ShuttleCargo::Water,10));
    CHECK(w.world().total_ore()==initial_mass);
    // A module not physically loaded on the shuttle cannot be constructed
    // from ship inventory on the planetary surface.
    CHECK(!w.transfer_planetary_housing_module(8,ship,key,true));
    bool failed_injected=false;
    try{
        (void)w.transfer_planetary_housing_module(7,ship,key,true,
                                                  FailPoint::AfterSnapshotWrite);
    }catch(const std::exception&){failed_injected=true;}
    CHECK(failed_injected);
    CHECK(w.world().planetary_shuttles().at(key).housing_module==0);
    CHECK(w.world().total_ore()==initial_mass);
    CHECK(w.transfer_planetary_housing_module(7,ship,key,true));
    CHECK(w.world().planetary_shuttles().at(key).housing_module==1);
    const auto manifest=w.world().planetary_transport_manifest(key);
    CHECK(manifest && manifest->valid());
    CHECK(transport::contains_good(*manifest,transport::GoodKind::Equipment,
          transport::kPlanetaryHousingModuleEquipmentId,1));
    CHECK(w.world().total_ore()==initial_mass);
    const auto in_orbit=w.world().canonical_state();
    CHECK(in_orbit.rfind("galaxy-kernel-v109|",0)==0);
    CHECK(Simulation::from_snapshot(in_orbit).canonical_state()==in_orbit);
    CHECK(w.launch_planetary_shuttle(7,ship,key,81001,false));
    // Landing must be completed before material can enter the site store.
    CHECK(!w.transfer_planetary_housing_module(7,ship,key,false));
    w.steps(3);
    CHECK(w.world().planetary_shuttles().at(key).flight_phase==2);
    CHECK(w.transfer_planetary_housing_module(7,ship,key,false));
    CHECK(w.world().planetary_sites().at(key).housing_modules==1);
    CHECK(w.world().total_ore()==initial_mass);
    CHECK(!w.start_planetary_housing(7,ship,key,81200));
    CHECK(w.launch_planetary_shuttle(7,ship,key,81002,true));
    w.steps(3);
    CHECK(w.world().planetary_shuttles().at(key).flight_phase==0);
    CHECK(w.transfer_planetary_housing_module(7,ship,key,true));
    CHECK(w.load_planetary_shuttle(7,ship,key,ShuttleCargo::Water,30));
    CHECK(w.load_planetary_shuttle(7,ship,key,ShuttleCargo::Food,30));
    CHECK(w.load_planetary_shuttle(7,ship,key,ShuttleCargo::Oxygen,30));
    CHECK(w.launch_planetary_shuttle(7,ship,key,81003,false));
    w.steps(3);
    CHECK(w.transfer_planetary_housing_module(7,ship,key,false));
    CHECK(w.unload_planetary_shuttle(7,ship,key,ShuttleCargo::Water,30));
    CHECK(w.unload_planetary_shuttle(7,ship,key,ShuttleCargo::Food,30));
    CHECK(w.unload_planetary_shuttle(7,ship,key,ShuttleCargo::Oxygen,30));
    CHECK(w.world().planetary_shuttles().at(key).oxygen_kg==0);
    CHECK(w.world().planetary_sites().at(key).oxygen_kg>=30);
    // All three original supplies physically landed via the shuttle,
    // with no direct ship-to-surface provision shortcut.
    CHECK(w.world().planetary_sites().at(key).housing_modules==2);
    CHECK(w.world().total_ore()==initial_mass);
    CHECK(!w.start_planetary_housing(8,ship,key,81200));
    CHECK(w.start_planetary_housing(7,ship,key,81200));
    CHECK(!w.start_planetary_housing(7,ship,key,81200));
    CHECK(w.world().planetary_sites().at(key).housing_modules==0);
    CHECK(w.world().planetary_sites().at(key).housing_phase==1);
    CHECK(w.world().total_ore()==initial_mass);
    w.steps(10);
    CHECK(!w.set_planetary_housing_pause(8,ship,key,true));
    CHECK(w.set_planetary_housing_pause(7,ship,key,true));
    CHECK(!w.set_planetary_housing_pause(7,ship,key,true));
    const auto paused_progress=w.world().planetary_sites().at(key).housing_progress;
    w.steps(10);
    CHECK(w.world().planetary_sites().at(key).housing_progress==paused_progress);
    CHECK(w.world().planetary_sites().at(key).housing_stalled_ticks>=10);
    CHECK(w.set_planetary_housing_pause(7,ship,key,false));
    w.steps(10);
    CHECK(w.world().planetary_sites().at(key).housing_progress==20);
    const auto provisional=w.world().canonical_state();
    CHECK(Simulation::from_snapshot(provisional).canonical_state()==provisional);
    const auto pending=(dir/"construction_pending.sqlite").string();
    PersistentWorld::recover_copy(db,pending);
    PersistentWorld recovered(pending,Simulation(3));
    CHECK(recovered.world().state_hash()==w.world().state_hash());
    w.steps(60);
    recovered.steps(60);
    CHECK(recovered.world().state_hash()==w.world().state_hash());
    CHECK(w.world().planetary_sites().at(key).housing_phase==2);
    CHECK(w.world().planetary_sites().at(key).housing_progress==80);
    CHECK(w.world().planetary_sites().at(key).housing_completed_tick>0);
    CHECK(!w.set_planetary_housing_pause(7,ship,key,true));
    CHECK(w.world().total_ore()==initial_mass);
    // Existing original two named on-surface workers may become founders only
    // with trusted owner approval, explicit attested consent and housing.
    CHECK(!w.settle_planetary_founder(8,ship,key,roster[0],81300,true));
    CHECK(!w.settle_planetary_founder(7,ship,key,roster[0],81300,false));
    CHECK(w.settle_planetary_founder(7,ship,key,roster[0],81300,true));
    CHECK(!w.settle_planetary_founder(7,ship,key,roster[0],81300,true));
    CHECK(!w.settle_planetary_founder(7,ship,key,roster[1],81300,true));
    CHECK(w.settle_planetary_founder(7,ship,key,roster[1],81301,true));
    CHECK(recovered.settle_planetary_founder(7,ship,key,roster[0],81300,true));
    CHECK(recovered.settle_planetary_founder(7,ship,key,roster[1],81301,true));
    CHECK(recovered.world().state_hash()==w.world().state_hash());
    Command forged_reuse{};
    forged_reuse.id=81300;
    forged_reuse.sequence=81300;
    forged_reuse.execute_tick=w.world().tick()+1;
    forged_reuse.issuer=7;forged_reuse.actor=ship;
    forged_reuse.action=Action::Thrust;
    CHECK(!w.submit(forged_reuse)); // original founder admission owns this ID
    CHECK((w.world().npc_records().at(roster[0]).legal_residence()==
        transport::Endpoint{transport::LocationKind::PlanetarySite,
                            w.world().planetary_sites().at(key).surface_location}));
    CHECK(w.world().total_ore()==initial_mass);
    const auto occupied=colony03::audit_planetary_surface(w.world(),7,ship,key);
    const auto supplied=colony03::assess_surface_supplies(w.world(),7,ship,key);
    // The genuinely landed shuttle pilot is a third person consuming site
    // stock. A two-worker-only forecast is overoptimistic and must not
    // survive this original-world housing/SQLite acceptance case.
    CHECK(occupied && occupied->current_surface_consumers==3);
    CHECK(supplied.current_surface_consumers==3);
    CHECK(supplied.baseline_ten_tick_food_periods==
          w.world().planetary_sites().at(key).food_kg/3);
    CHECK(supplied.baseline_ten_tick_water_periods==
          w.world().planetary_sites().at(key).water_kg/3);
    CHECK(supplied.baseline_ten_tick_oxygen_periods==
          w.world().planetary_sites().at(key).oxygen_kg/3);
    CHECK(supplied.gate==colony03::SurfaceSupplyGate::ImportDependent);
    CHECK(supplied.baseline_ten_tick_reserve_periods>=1);
    CHECK(!supplied.independent_power_proven &&
          !supplied.closed_water_loop_proven &&
          !supplied.independently_sustainable);
    const auto audit=colony03::audit_planetary_surface(
        w.world(),7,ship,key);
    CHECK(audit && audit->permanent_housing_installed &&
          audit->housing_order_id==81200 && audit->unused_housing_capacity==2 &&
          audit->original_permanent_residents==2 &&
          audit->physical_delegates==2);
    // A direct tampered record cannot invent an impossible construction tick.
    auto corrupted=w.world().canonical_state();
    const auto first=corrupted.find("planetsite|");
    CHECK(first!=std::string::npos);
    const auto end=corrupted.find('\n',first);
    const auto field=corrupted.rfind('|',end);
    CHECK(field!=std::string::npos && field>first);
    corrupted.replace(field+1,end-field-1,"81300"); // duplicate consent receipt
    bool denied=false;
    try{(void)Simulation::from_snapshot(corrupted);}
    catch(const std::exception&){denied=true;}
    CHECK(denied);
    // WO3-E: a third original adult makes a real passenger trip. Arrival
    // proof, capacity and voluntary owner attestations authorize residency,
    // while ordinary shuttle reboard may not circumvent legal departure.
    CHECK(roster.size()>=4 && roster[3]!=roster[2]);
    CHECK(w.launch_planetary_shuttle(7,ship,key,81004,true));
    w.steps(3);
    CHECK(w.book_planetary_shuttle_passenger(7,ship,key,roster[3],true));
    for(const auto cargo:{ShuttleCargo::Water,ShuttleCargo::Food,
                         ShuttleCargo::Oxygen})
        CHECK(w.load_planetary_shuttle(7,ship,key,cargo,20));
    CHECK(w.launch_planetary_shuttle(7,ship,key,81005,false));
    w.steps(3);
    CHECK(w.transfer_planetary_shuttle_visitor(7,ship,key,roster[3],false));
    for(const auto cargo:{ShuttleCargo::Water,ShuttleCargo::Food,
                         ShuttleCargo::Oxygen})
        CHECK(w.unload_planetary_shuttle(7,ship,key,cargo,20));
    const auto adult_preflight=colony03::assess_planetary_household(
        w.world(),7,ship,key,roster[3]);
    CHECK(adult_preflight.independent_adult_can_apply() &&
          adult_preflight.original_shuttle_arrival_verified &&
          adult_preflight.registered_dependent_ids.empty() &&
          !adult_preflight.requires_art02_atomic_family_transaction &&
          adult_preflight.required_beds==1 &&
          adult_preflight.available_beds==2);
    CHECK(!colony03::assess_planetary_household(
        w.world(),8,ship,key,roster[3]).independent_adult_can_apply());
    CHECK(!w.settle_planetary_visitor(8,ship,key,roster[3],81501,true));
    CHECK(!w.settle_planetary_visitor(7,ship,key,roster[3],81501,false));
    bool rollback_visitor=false;
    try {
        (void)w.settle_planetary_visitor(7,ship,key,roster[3],81501,
            true,FailPoint::AfterSnapshotWrite);
    }catch(const std::exception&){rollback_visitor=true;}
    CHECK(rollback_visitor);
    CHECK(w.world().npc_records().at(roster[3]).legal_residence()==
          (transport::Endpoint{transport::LocationKind::ColonyShip,ship}));
    CHECK(w.settle_planetary_visitor(7,ship,key,roster[3],81501,true));
    // Resident audit must count one admitted visitor exactly once:
    // 2 original delegates plus this consenting adult occupy 3/4 beds.
    const auto visitor_beds=colony03::audit_planetary_surface(
        w.world(),7,ship,key);
    CHECK(visitor_beds && visitor_beds->unused_housing_capacity==1 &&
          visitor_beds->original_permanent_residents==3 &&
          visitor_beds->physical_delegates==2);
    CHECK(colony03::assess_planetary_household(
        w.world(),7,ship,key,roster[3]).gate==
        colony03::PlanetaryHouseholdGate::ExistingSurfaceResident);
    CHECK(!w.settle_planetary_visitor(7,ship,key,roster[3],81501,true));
    CHECK(!w.transfer_planetary_shuttle_visitor(7,ship,key,roster[3],true));
    CHECK(w.world().canonical_state().rfind("galaxy-kernel-v111|",0)==0);
    const auto resident_snapshot=w.world().canonical_state();
    CHECK(Simulation::from_snapshot(resident_snapshot).canonical_state()==
          resident_snapshot);
    // A forged visitor admission cannot reuse a genuine flight receipt.
    // Parser must reject it even when all person/location fields still match.
    auto forged_resident=resident_snapshot;
    const auto visitor_marker=forged_resident.find("planetsettler|");
    CHECK(visitor_marker!=std::string::npos);
    const auto consent_marker=forged_resident.find("|81501|0\n",visitor_marker);
    CHECK(consent_marker!=std::string::npos);
    forged_resident.replace(consent_marker,9,"|81004|0\n");
    bool forged_consent_rejected=false;
    try{(void)Simulation::from_snapshot(forged_resident);}
    catch(const std::exception&){forged_consent_rejected=true;}
    CHECK(forged_consent_rejected);
    const auto residence_copy=(dir/"visitor_resident.sqlite").string();
    PersistentWorld::recover_copy(db,residence_copy);
    {
        PersistentWorld resident(residence_copy,Simulation(10));
        CHECK(resident.world().state_hash()==w.world().state_hash());
        CHECK(resident.world().npc_records().at(roster[3]).legal_residence()==
              (transport::Endpoint{transport::LocationKind::PlanetarySite,
                  w.world().planetary_sites().at(key).surface_location}));
        CHECK(!resident.depart_planetary_visitor(
            7,ship,key,roster[3],81502,false));
    }
    CHECK(!w.depart_planetary_visitor(8,ship,key,roster[3],81502,true));
    CHECK(!w.depart_planetary_visitor(7,ship,key,roster[3],81502,false));
    CHECK(w.depart_planetary_visitor(7,ship,key,roster[3],81502,true));
    const auto departed_beds=colony03::audit_planetary_surface(
        w.world(),7,ship,key);
    CHECK(departed_beds && departed_beds->unused_housing_capacity==2);
    CHECK(!w.depart_planetary_visitor(7,ship,key,roster[3],81502,true));
    CHECK(w.world().npc_records().at(roster[3]).legal_residence()==
          (transport::Endpoint{transport::LocationKind::ColonyShip,ship}));
    CHECK(std::binary_search(
        w.world().planetary_shuttles().at(key).passenger_ids.begin(),
        w.world().planetary_shuttles().at(key).passenger_ids.end(),roster[3]));
    CHECK(w.world().total_ore()==initial_mass);
    CHECK(Simulation::from_snapshot(w.world().canonical_state()).state_hash()==
          w.world().state_hash());
    CHECK(w.launch_planetary_shuttle(7,ship,key,81006,true));
    w.steps(3);
    CHECK(w.world().npc_records().at(roster[3]).location==ship);
    // WO3-E: existing physical dependent identity is NOT an ART02
    // custody authorization or a shuttle child berth. Audit the actual
    // 3-seat shuttle before allowing any household writer to be added.
    auto household_probe=Simulation::from_snapshot(w.world().canonical_state());
    CHECK(household_probe.book_planetary_shuttle_passenger(
        7,ship,key,roster[3],false)); // genuine returned adult, unbooked
    const auto register_probe_child=[&](std::uint64_t cmd,std::uint64_t child){
        Command birth{};birth.id=cmd;birth.sequence=cmd;
        birth.execute_tick=household_probe.tick()+1;
        birth.issuer=Simulation::kServerAuthority;birth.actor=ship;birth.target=ship;
        birth.action=Action::HostRegisterDependent;
        birth.transport_id=child;birth.passenger_ids={roster[3]};
        CHECK(household_probe.submit(birth));
        household_probe.step();
        CHECK(household_probe.physical_dependents().count(child)==1);
    };
    register_probe_child(83001,(std::uint64_t{1}<<62)+301);
    const auto family_one=colony03::assess_planetary_household(
        household_probe,7,ship,key,roster[3]);
    CHECK(family_one.registered_dependent_ids.size()==1 &&
          family_one.all_dependents_physically_with_guardian &&
          family_one.requires_art02_atomic_family_transaction &&
          family_one.required_beds==2 &&
          family_one.required_shuttle_passenger_seats==2 &&
          family_one.physical_shuttle_passenger_capacity==3 &&
          !family_one.independent_adult_can_apply());
    CHECK(!household_probe.book_planetary_shuttle_passenger(
        7,ship,key,roster[3],true));
    register_probe_child(83002,(std::uint64_t{1}<<62)+302);
    register_probe_child(83003,(std::uint64_t{1}<<62)+303);
    const auto family_overflow=colony03::assess_planetary_household(
        household_probe,7,ship,key,roster[3]);
    CHECK(family_overflow.required_beds==4 &&
          family_overflow.required_shuttle_passenger_seats==4 &&
          family_overflow.gate==
            colony03::PlanetaryHouseholdGate::DependentBerthOverflow &&
          !family_overflow.independent_adult_can_apply());
    CHECK(Simulation::from_snapshot(household_probe.canonical_state()).
          canonical_state()==household_probe.canonical_state());
    const auto final_copy=(dir/"housing_finished.sqlite").string();
    PersistentWorld::recover_copy(db,final_copy);
    PersistentWorld final_world(final_copy,Simulation(4));
    CHECK(final_world.world().state_hash()==w.world().state_hash());
    CHECK(final_world.world().total_ore()==initial_mass);
    // An independently recovered, unresupplied colony cannot silently
    // manufacture calories, drinking water or oxygen. It remains an original
    // permanently housed site but its provision counter records shortages.
    const auto shortage_db=(dir/"housing_unresupplied.sqlite").string();
    PersistentWorld::recover_copy(db,shortage_db);
    {
        PersistentWorld depleted(shortage_db,Simulation(8));
        depleted.steps(300);
        const auto& now=depleted.world().planetary_sites().at(key);
        CHECK(now.housing_phase==2 && now.housing_shortages>0);
        CHECK(now.food_kg>=0 && now.water_kg>=0 && now.oxygen_kg>=0);
        CHECK(depleted.world().total_ore()==initial_mass);
        const auto shortage_audit=colony03::assess_surface_supplies(
            depleted.world(),7,ship,key);
        CHECK(!shortage_audit.independently_sustainable);
        CHECK(shortage_audit.past_shortage_cycles>0);
    }
    // Temp directory is removed when SQLite handles have closed (Windows-safe).
    std::cout<<"WO3-B native original planetary module deliveries, "
                "staffed housing, tamper and SQLite recovery PASS\n";
    return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
