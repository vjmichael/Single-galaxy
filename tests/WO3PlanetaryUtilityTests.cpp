#include "galaxy/WO3ColonyOperations.hpp"
#include "galaxy/WO2IndustryWorkforce.hpp"
#include "galaxy/Persistence.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>
#include <utility>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(\
    "WO3-C native line "+std::to_string(__LINE__)+": " #x);}while(0)
struct Choice {std::uint32_t system;std::uint64_t body;std::uint16_t lat,lon;};
static Choice choose() {
    for(std::uint32_t sys=1;sys<=2000;++sys) {
        Simulation p(613,sys);
        const auto ship=p.add_colony_ship(7,{0,0},200,8);
        p.provision_starter_exploration_scanner(ship);
        p.ensure_physical_celestial_systems();
        for(const auto& b:p.physical_celestial_systems().at(sys)) {
            if(b.kind!=CelestialKind::Planet && b.kind!=CelestialKind::Moon)continue;
            for(const auto [lat,lon]:std::vector<std::pair<unsigned,unsigned>>{
                 {17,36},{17,18},{18,46},{16,60},{14,4},{20,32},
                 {10,27},{26,52},{7,15},{28,8},{12,40},{24,55}}) {
                const auto region=p.scan_planetary_geography(
                    7,ship,b.id,std::uint16_t(lat),std::uint16_t(lon));
                if(region && !region->landscape.region.surface_water &&
                   region->landscape.region.farming_possible)
                    return {sys,b.id,std::uint16_t(lat),std::uint16_t(lon)};
            }
        }
    }
    throw std::runtime_error("missing real buildable site");
}
static void act(PersistentWorld& w,EntityId ship,std::uint64_t id,Action kind,
                EntityId target=0,std::int64_t amount=1,int opt=0) {
    Command c{};c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
    c.issuer=7;c.actor=ship;c.target=target;c.action=kind;
    c.amount=amount;c.thrust_x=opt;
    CHECK(w.submit(c));w.step();
    CHECK(w.world().events().back().command_id==id &&
          w.world().events().back().result==Outcome::Applied);
}
static void down(PersistentWorld& w,EntityId ship,const PlanetarySiteKey& key,
                 std::uint64_t id) {
    CHECK(w.launch_planetary_shuttle(7,ship,key,id,false));w.steps(3);
    CHECK(w.world().planetary_shuttles().at(key).flight_phase==2);
}
static void up(PersistentWorld& w,EntityId ship,const PlanetarySiteKey& key,
               std::uint64_t id) {
    CHECK(w.launch_planetary_shuttle(7,ship,key,id,true));w.steps(3);
    CHECK(w.world().planetary_shuttles().at(key).flight_phase==0);
}
int main() {
 try {
    const auto chosen=choose();
    const PlanetarySiteKey key{chosen.body,chosen.lat,chosen.lon,1};
    const auto folder=fs::temp_directory_path()/
        ("wo3_utility_"+std::to_string(std::chrono::steady_clock::now().
                                      time_since_epoch().count()));
    fs::create_directories(folder);
    struct Cleanup {fs::path p;~Cleanup(){std::error_code ec;fs::remove_all(p,ec);}} cleanup{folder};
    const auto db=(folder/"world.sqlite").string();
    Simulation seed(613,chosen.system);
    const auto ship=seed.add_colony_ship(7,{0,0},200,8);
    seed.provision_colony_genesis(ship,1000,3000,1000,1000,1000);
    seed.provision_starter_exploration_scanner(ship);
    const auto bulk=seed.add_asteroid({2000,0},150);
    const auto silicate=seed.add_geological_asteroid({1000,0},
        AsteroidClass::Silicate,30,2500);
    seed.enable_individual_workforce();
    seed.ensure_physical_celestial_systems();
    seed.ensure_ship_frame_registry();
    const auto initial_mass=seed.total_ore();
    PersistentWorld w(db,std::move(seed));
    act(w,ship,1,Action::Mine,bulk,80);
    act(w,ship,2,Action::RefineOre,0,80);
    act(w,ship,3,Action::FabricateModule,0,20);
    act(w,ship,4,Action::ConstructOutpost);
    const auto orbital=w.world().built_outposts().begin()->first;
    act(w,ship,6,Action::ConstructWorkshop,orbital);
    act(w,ship,8,Action::ConstructUtility,orbital);
    act(w,ship,9,Action::ConstructHabitat,orbital);
    for(int i=1;i<=3;++i)act(w,ship,9+i,Action::TransferHabitatStock,orbital,100,i);
    act(w,ship,13,Action::AssignHabitatResidents,orbital,2);
    act(w,ship,14,Action::AssignUtilityTechnicians,orbital,1);
    act(w,ship,15,Action::TransferUtilitySpares,orbital,5);
    act(w,ship,16,Action::SetUtilityMaintenance,orbital,1);
    act(w,ship,19,Action::Mine,silicate,30);
    act(w,ship,20,Action::RefineMineral,10,12000);
    act(w,ship,21,Action::TransferWorkshopFeed,orbital,1000);
    auto slots=workforce02::plan_original_workshop(w.world(),7,ship,orbital);
    CHECK(slots[0].can_offer_assignment());
    auto operator_id=slots[0].suggested_npc;
    auto assignment=workforce02::approved_original_assignment(
        w.world(),7,ship,orbital,false,operator_id,22,true);
    CHECK(assignment && w.submit(*assignment));w.step();
    CHECK(w.world().events().back().result==Outcome::Applied);
    act(w,ship,24,Action::ConstructWorkshopFinisher,orbital);
    w.steps(8);
    CHECK(w.world().workshop(orbital)->panels_kg>=3);
    slots=workforce02::plan_original_workshop(w.world(),7,ship,orbital);
    const auto builder=slots[1].suggested_npc;
    CHECK(builder && builder!=operator_id);
    for(std::uint64_t id=280;id<283;++id) {
        auto training=workforce02::approved_original_trade_training(
            w.world(),7,ship,orbital,builder,WorkshopTrade::Shape,id,true);
        CHECK(training && w.submit(*training));w.step();
        CHECK(w.world().events().back().result==Outcome::Applied);
    }
    CHECK(w.world().workshop_trade_certified(builder,WorkshopTrade::Shape));
    act(w,ship,290,Action::AssignUtilityTechnicians,orbital,0);
    act(w,ship,291,Action::AssignHabitatResidents,orbital,-2);
    // A second actual named worker earns three physical Shape credits
    // before deployment. Supervisor relief must not conjure a certificate.
    std::uint64_t trained_partner=0;
    for(const auto& [id,n]:w.world().npc_records()) {
        if(id!=builder && id!=operator_id && n.home_ship==ship &&
           n.location==ship && n.role==NpcRole::Ship &&
           w.world().npc_available_for_duty(id)) {
            trained_partner=id;
            break;
        }
    }
    CHECK(trained_partner);
    w.steps(60);
    CHECK(w.world().workshop(orbital)->panels_kg>=3);
    for(std::uint64_t id=310;id<313;++id) {
        const auto training=workforce02::approved_original_trade_training(
            w.world(),7,ship,orbital,trained_partner,WorkshopTrade::Shape,id,true);
        CHECK(training && w.submit(*training));w.step();
        CHECK(w.world().events().back().result==Outcome::Applied);
    }
    CHECK(w.world().workshop_trade_certified(
        trained_partner,WorkshopTrade::Shape));
    std::uint64_t trained_visitor=0;
    for(const auto& [id,n]:w.world().npc_records()) {
        if(id!=builder && id!=trained_partner && id!=operator_id &&
           n.home_ship==ship && n.location==ship &&
           n.role==NpcRole::Ship && w.world().npc_available_for_duty(id)) {
            trained_visitor=id;
            break;
        }
    }
    CHECK(trained_visitor);
    w.steps(60);
    CHECK(w.world().workshop(orbital)->panels_kg>=3);
    for(std::uint64_t id=320;id<323;++id) {
        const auto training=workforce02::approved_original_trade_training(
            w.world(),7,ship,orbital,trained_visitor,WorkshopTrade::Shape,id,true);
        CHECK(training && w.submit(*training));w.step();
        CHECK(w.world().events().back().result==Outcome::Applied);
    }
    CHECK(w.world().workshop_trade_certified(
        trained_visitor,WorkshopTrade::Shape));
    CHECK(w.world().npc_records().at(builder).location==ship);
    CHECK(w.world().npc_available_for_duty(builder));
    CHECK(w.begin_celestial_transit(7,ship,chosen.body,90001));
    w.steps(4);CHECK(w.complete_celestial_transit(90001));
    std::vector<std::uint64_t> crew;
    for(const auto& [id,n]:w.world().npc_records())
        if(n.home_ship==ship && n.location==ship && n.role==NpcRole::Ship)
            crew.push_back(id);
    CHECK(crew.size()>=4);
    const auto partner=trained_partner;
    CHECK(std::find(crew.begin(),crew.end(),partner)!=crew.end());
    CHECK(w.build_planetary_site(7,ship,chosen.body,chosen.lat,chosen.lon,
                                builder,partner));
    w.steps(4);
    CHECK(w.support_planetary_crew(7,ship,chosen.body,chosen.lat,chosen.lon,false,10));
    CHECK(w.support_planetary_crew(7,ship,chosen.body,chosen.lat,chosen.lon,true,10));
    w.steps(240);
    CHECK(w.world().planetary_spaceport_operational(
        chosen.body,chosen.lat,chosen.lon));
    CHECK(w.build_planetary_shuttle(7,ship,key));
    const auto pilot=*std::find_if(crew.begin(),crew.end(),[&](auto id){
        return id!=builder && id!=partner;
    });
    CHECK(w.assign_planetary_shuttle_pilot(7,ship,key,pilot));
    CHECK(w.load_planetary_shuttle(7,ship,key,ShuttleCargo::Water,10));
    // Deliver actual first two construction modules on two original trips.
    CHECK(w.transfer_planetary_housing_module(7,ship,key,true));
    down(w,ship,key,81001);
    CHECK(w.transfer_planetary_housing_module(7,ship,key,false));
    up(w,ship,key,81002);
    CHECK(w.transfer_planetary_housing_module(7,ship,key,true));
    CHECK(w.load_planetary_shuttle(7,ship,key,ShuttleCargo::Water,30));
    CHECK(w.load_planetary_shuttle(7,ship,key,ShuttleCargo::Food,30));
    CHECK(w.load_planetary_shuttle(7,ship,key,ShuttleCargo::Oxygen,30));
    down(w,ship,key,81003);
    CHECK(w.transfer_planetary_housing_module(7,ship,key,false));
    for(auto cargo:{ShuttleCargo::Water,ShuttleCargo::Food,ShuttleCargo::Oxygen})
        CHECK(w.unload_planetary_shuttle(7,ship,key,cargo,30));
    CHECK(w.start_planetary_housing(7,ship,key,81200));
    w.steps(80);
    CHECK(w.world().planetary_sites().at(key).housing_phase==2);
    CHECK(!w.start_planetary_surface_utility(8,ship,key,81400,builder));
    CHECK(!w.start_planetary_surface_utility(7,ship,key,81400,pilot));
    CHECK(!w.start_planetary_surface_utility(7,ship,key,81400,builder));
    // Two MORE manufactured modules must physically reach the existing site.
    up(w,ship,key,81004);
    CHECK(w.transfer_planetary_housing_module(7,ship,key,true));
    down(w,ship,key,81005);
    CHECK(w.transfer_planetary_housing_module(7,ship,key,false));
    up(w,ship,key,81006);
    CHECK(w.book_planetary_shuttle_passenger(
        7,ship,key,trained_visitor,true));
    CHECK(w.transfer_planetary_housing_module(7,ship,key,true));
    CHECK(w.load_planetary_shuttle(7,ship,key,ShuttleCargo::Water,30));
    CHECK(w.load_planetary_shuttle(7,ship,key,ShuttleCargo::Food,30));
    CHECK(w.load_planetary_shuttle(7,ship,key,ShuttleCargo::Oxygen,30));
    down(w,ship,key,81007);
    CHECK(w.transfer_planetary_housing_module(7,ship,key,false));
    for(auto cargo:{ShuttleCargo::Water,ShuttleCargo::Food,ShuttleCargo::Oxygen})
        CHECK(w.unload_planetary_shuttle(7,ship,key,cargo,30));
    CHECK(w.transfer_planetary_shuttle_visitor(
        7,ship,key,trained_visitor,false));
    CHECK(w.assign_planetary_shuttle_visitor_duty(
        7,ship,key,trained_visitor,true));
    CHECK(w.world().planetary_sites().at(key).housing_modules==2);
    CHECK(w.world().total_ore()==initial_mass);
    bool failed=false;
    try {
        (void)w.start_planetary_surface_utility(
            7,ship,key,81400,builder,FailPoint::AfterSnapshotWrite);
    } catch(const std::exception&) {failed=true;}
    CHECK(failed);
    CHECK(w.world().planetary_sites().at(key).utility_phase==0);
    CHECK(w.start_planetary_surface_utility(7,ship,key,81400,builder));
    CHECK(!w.start_planetary_surface_utility(7,ship,key,81400,builder));
    CHECK(w.world().total_ore()==initial_mass);
    CHECK(w.world().canonical_state().rfind("galaxy-kernel-v110|",0)==0);
    w.steps(10);
    CHECK(w.pause_planetary_surface_utility(7,ship,key,true));
    const auto progress=w.world().planetary_sites().at(key).utility_progress;
    w.steps(7);
    CHECK(w.world().planetary_sites().at(key).utility_progress==progress);
    CHECK(w.pause_planetary_surface_utility(7,ship,key,false));
    w.steps(10);
    const auto before_handover=w.world().planetary_sites().at(key);
    CHECK(before_handover.utility_phase==1);
    const auto historical_v110=w.world().canonical_state();
    CHECK(historical_v110.rfind("galaxy-kernel-v110|",0)==0);
    CHECK(Simulation::from_snapshot(historical_v110).canonical_state()==
          historical_v110);
    // Native backward-compatibility: a pre-relief v110 utility job remains
    // readable without inventing any handover receipts.
    CHECK(!w.handover_planetary_surface_utility(
        8,ship,key,builder,partner,81600));
    CHECK(!w.handover_planetary_surface_utility(
        7,ship,key,pilot,partner,81600));
    bool failed_handover=false;
    try {
        (void)w.handover_planetary_surface_utility(
            7,ship,key,builder,partner,81600,FailPoint::AfterSnapshotWrite);
    }catch(const std::exception&){failed_handover=true;}
    CHECK(failed_handover);
    CHECK(w.world().planetary_sites().at(key).utility_supervisor==builder);
    CHECK(w.handover_planetary_surface_utility(
        7,ship,key,builder,partner,81600));
    CHECK(!w.handover_planetary_surface_utility(
        7,ship,key,builder,partner,81600));
    const auto& after_handover=w.world().planetary_sites().at(key);
    CHECK(after_handover.utility_supervisor==partner &&
          after_handover.utility_order_id==before_handover.utility_order_id &&
          after_handover.utility_progress==before_handover.utility_progress &&
          after_handover.embodied_ore==before_handover.embodied_ore &&
          after_handover.utility_handovers.size()==1 &&
          after_handover.utility_handovers.front().request_id==81600);
    CHECK(w.world().total_ore()==initial_mass);
    // Same-tick cross-orders must never mutate the on-site incumbent;
    // later bounded handovers retain the same original construction receipt.
    CHECK(!w.handover_planetary_surface_utility(
        7,ship,key,partner,builder,81601));
    w.step();
    CHECK(!w.handover_planetary_surface_utility(
        7,ship,key,partner,builder,81600)); // already used approval ID
    CHECK(w.handover_planetary_surface_utility(
        7,ship,key,partner,builder,81601));
    const auto& second=w.world().planetary_sites().at(key);
    CHECK(second.utility_supervisor==builder &&
          second.utility_handovers.size()==2 &&
          second.utility_handovers[0].replacement==partner &&
          second.utility_handovers[1].previous==partner &&
          second.utility_handovers[1].replacement==builder &&
          second.utility_handovers[0].tick<
              second.utility_handovers[1].tick &&
          second.utility_progress==before_handover.utility_progress+1 &&
          second.embodied_ore==before_handover.embodied_ore);
    // A qualified worker who arrived on the ORIGINAL shuttle and was
    // explicitly granted temporary surface duty can supply a real relief.
    w.step();
    CHECK(w.handover_planetary_surface_utility(
        7,ship,key,builder,trained_visitor,81602));
    const auto visitor_progress=w.world().planetary_sites().at(key).utility_progress;
    CHECK(w.assign_planetary_shuttle_visitor_duty(
        7,ship,key,trained_visitor,false));
    w.step();
    CHECK(w.world().planetary_sites().at(key).utility_progress==visitor_progress);
    CHECK(w.assign_planetary_shuttle_visitor_duty(
        7,ship,key,trained_visitor,true));
    w.step();
    CHECK(w.world().planetary_sites().at(key).utility_progress==visitor_progress+1);
    CHECK(w.world().planetary_sites().at(key).utility_handovers.size()==3);
    const auto active_relief=colony03::audit_planetary_surface(w.world(),7,ship,key);
    CHECK(active_relief && active_relief->utility_handover_count==3 &&
          active_relief->utility_last_handover_id==81602 &&
          active_relief->utility_supervisor==trained_visitor &&
          active_relief->utility_supervisor_present &&
          active_relief->utility_supervisor_certified &&
          active_relief->utility_supervisor_has_temp_duty);
    Command replay{};
    replay.id=81602;
    replay.sequence=81602;
    replay.execute_tick=w.world().tick()+1;
    replay.issuer=7;
    replay.actor=ship;
    replay.action=Action::Thrust;
    CHECK(!w.submit(replay)); // host cannot reuse immutable relief proof
    CHECK(w.world().canonical_state().rfind("galaxy-kernel-v112|",0)==0);
    CHECK(Simulation::from_snapshot(w.world().canonical_state()).state_hash()==
          w.world().state_hash());
    // A forged last replacement without its real chain must be rejected.
    auto forged=w.world().canonical_state();
    const auto handover=forged.find("planetutilityhandover|");
    CHECK(handover!=std::string::npos);
    const auto eol=forged.find('\n',handover);
    const auto last=forged.rfind('|',eol);
    CHECK(last!=std::string::npos);
    forged.replace(last+1,eol-last-1,"999999");
    bool denied=false;
    try {(void)Simulation::from_snapshot(forged);}
    catch(const std::exception&){denied=true;}
    CHECK(denied);
    const auto pending=(folder/"utility_pending.sqlite").string();
    PersistentWorld::recover_copy(db,pending);
    PersistentWorld recovered(pending,Simulation(8));
    CHECK(recovered.world().state_hash()==w.world().state_hash());
    w.steps(40);recovered.steps(40);
    CHECK(w.world().state_hash()==recovered.world().state_hash());
    CHECK(w.world().planetary_sites().at(key).utility_phase==2);
    w.steps(60);
    const auto& utility=w.world().planetary_sites().at(key);
    CHECK(utility.utility_progress==60);
    CHECK(utility.utility_powered_cycles>0 && utility.hvac_cycles>0);
    CHECK(utility.water_recycled_kg>0 && utility.brine_kg>0);
    CHECK(utility.oxygen_recycled_kg>0);
    CHECK(w.world().total_ore()==initial_mass);
    const auto supply=colony03::assess_surface_supplies(w.world(),7,ship,key);
    CHECK(supply.independent_power_proven && supply.hvac_ever_operated &&
          supply.loss_bearing_recycling_operated &&
          !supply.closed_water_loop_proven &&
          !supply.independently_sustainable);
    const auto saved=w.world().canonical_state();
    CHECK(Simulation::from_snapshot(saved).canonical_state()==saved);
    const auto copy=(folder/"utility_final.sqlite").string();
    PersistentWorld::recover_copy(db,copy);
    PersistentWorld reread(copy,Simulation(9));
    CHECK(reread.world().state_hash()==w.world().state_hash());
    const auto before_production_embodied=
        w.world().planetary_sites().at(key).embodied_ore;
    // WO3-D/v113: farm production must now be physically constructed on
    // the planet from TWO fresh original shuttle-delivered module racks.
    // Existing orbital-ship modules cannot materialize into local structures.
    CHECK(!w.start_planetary_production_build(
        8,ship,key,true,81800,builder));
    CHECK(!w.start_planetary_production_build(
        7,ship,key,true,81800,pilot));
    CHECK(!w.start_planetary_production_build(
        7,ship,key,true,81800,builder)); // no landed stock
    up(w,ship,key,82010);
    CHECK(w.transfer_planetary_housing_module(7,ship,key,true));
    down(w,ship,key,82011);
    CHECK(w.transfer_planetary_housing_module(7,ship,key,false));
    CHECK(!w.start_planetary_production_build(
        7,ship,key,true,81800,builder)); // only one of two modules
    up(w,ship,key,82012);
    CHECK(w.transfer_planetary_housing_module(7,ship,key,true));
    // The next descent consumes one REAL kilogram of water propellant.
    // Carry 101 water so the delivered 100 remains physically available.
    CHECK(w.load_planetary_shuttle(7,ship,key,ShuttleCargo::Water,101));
    CHECK(w.load_planetary_shuttle(7,ship,key,ShuttleCargo::Food,100));
    CHECK(w.load_planetary_shuttle(7,ship,key,ShuttleCargo::Oxygen,100));
    down(w,ship,key,82013);
    CHECK(w.world().planetary_shuttles().at(key).water_kg>=100);
    CHECK(w.transfer_planetary_housing_module(7,ship,key,false));
    for(const auto cargo:{ShuttleCargo::Water,ShuttleCargo::Food,
                          ShuttleCargo::Oxygen})
        CHECK(w.unload_planetary_shuttle(7,ship,key,cargo,100));
    CHECK(w.world().planetary_sites().at(key).housing_modules==2);
    // WO3-D/v117: both additional facility families are built from this
    // exact TWO landed original modules. Independent copied SQLite worlds
    // are mutually exclusive alternatives, not cloned material in one world.
    // Each actual 40-tick job uses the same genuinely Shape-certified
    // original surface workers, original inventory and durable commit path.
    for(const bool annex:{true,false}) {
        const auto name=annex?"annex":"medical_support";
        const auto service_db=(folder/(std::string("service_")+name+".sqlite")).string();
        PersistentWorld::recover_copy(db,service_db);
        PersistentWorld service(service_db,Simulation(933));
        const auto service_order=annex?82500ULL:82600ULL;
        const auto relief_id=annex?82501ULL:82601ULL;
        const auto mass_before=service.world().total_ore();
        CHECK(!service.start_planetary_service_build(
            8,ship,key,annex,service_order,builder));
        CHECK(!service.start_planetary_service_build(
            7,ship,key,annex,service_order,pilot));
        bool failed_write=false;
        try {
            (void)service.start_planetary_service_build(
                7,ship,key,annex,service_order,builder,
                FailPoint::AfterSnapshotWrite);
        }catch(const std::exception&){failed_write=true;}
        CHECK(failed_write);
        CHECK(service.world().planetary_sites().at(key).
              service_projects[annex?0:1].phase==0);
        CHECK(service.start_planetary_service_build(
            7,ship,key,annex,service_order,builder));
        CHECK(service.world().planetary_sites().at(key).housing_modules==0);
        CHECK(service.world().total_ore()==mass_before);
        const auto begun=service.world().canonical_state();
        CHECK(begun.rfind("galaxy-kernel-v117|",0)==0);
        CHECK(Simulation::from_snapshot(begun).canonical_state()==begun);
        service.steps(12);
        CHECK(service.pause_planetary_service_build(
            7,ship,key,annex,true));
        const auto frozen=service.world().planetary_sites().at(key).
            service_projects[annex?0:1].progress;
        service.steps(5);
        CHECK(service.world().planetary_sites().at(key).
              service_projects[annex?0:1].progress==frozen);
        CHECK(!service.handover_planetary_service_build(
            8,ship,key,annex,builder,partner,relief_id));
        bool relief_fault=false;
        try {
            (void)service.handover_planetary_service_build(
                7,ship,key,annex,builder,partner,relief_id,
                FailPoint::AfterSnapshotWrite);
        }catch(const std::exception&){relief_fault=true;}
        CHECK(relief_fault);
        CHECK(service.handover_planetary_service_build(
            7,ship,key,annex,builder,partner,relief_id));
        CHECK(!service.handover_planetary_service_build(
            7,ship,key,annex,builder,partner,relief_id));
        const auto mid=(folder/(std::string("service_mid_")+name+".sqlite")).string();
        PersistentWorld::recover_copy(service_db,mid);
        PersistentWorld twin_service(mid,Simulation(939));
        CHECK(twin_service.world().state_hash()==service.world().state_hash());
        CHECK(service.pause_planetary_service_build(
            7,ship,key,annex,false));
        CHECK(twin_service.pause_planetary_service_build(
            7,ship,key,annex,false));
        service.steps(40);twin_service.steps(40);
        CHECK(service.world().state_hash()==twin_service.world().state_hash());
        const auto& built=service.world().planetary_sites().at(key).
            service_projects[annex?0:1];
        CHECK(built.phase==2 && built.progress==40 &&
              built.completed_tick>0 && built.supervisor==partner &&
              built.powered_cycles>0);
        const auto audit=colony03::audit_planetary_surface(
            service.world(),7,ship,key);
        CHECK(audit && audit->service_phases[annex?0:1]==2 &&
              audit->service_handover_count[annex?0:1]==1 &&
              audit->service_powered_cycles[annex?0:1]>0 &&
              audit->installed_beds==(annex?8:4) &&
              service.world().total_ore()==mass_before);
        CHECK(!service.start_planetary_service_build(
            7,ship,key,annex,service_order+10,partner));
        const auto accepted=service.world().canonical_state();
        CHECK(Simulation::from_snapshot(accepted).canonical_state()==accepted);
        auto forged=accepted;
        const auto start=forged.find("planetservice|");
        CHECK(start!=std::string::npos);
        const auto end=forged.find('\n',start);
        const auto id_pos=forged.find("|"+std::to_string(service_order)+"|",start);
        CHECK(id_pos!=std::string::npos && id_pos<end);
        forged.replace(id_pos+1,std::to_string(service_order).size(),"0");
        bool rejected=false;
        try{(void)Simulation::from_snapshot(forged);}
        catch(const std::exception&){rejected=true;}
        CHECK(rejected);
        CHECK(twin_service.world().total_ore()==mass_before);
        if(annex) {
            // WO3-E: genuinely built eight-bed annex, one original ART02
            // birth, BOTH real parents and the one physical registered child.
            // The signed regional + ART02 census transfer and world snapshot
            // must share ONE SQLite commit in both directions.
            const auto surface=service.world().planetary_sites().at(key).
                surface_location;
            CHECK(service.load_planetary_shuttle(
                7,ship,key,ShuttleCargo::Water,10));
            up(service,ship,key,82700);
            std::vector<std::uint64_t> parents;
            for(const auto& [id,n]:service.world().npc_records())
                if(n.home_ship==ship && n.location==ship &&
                   n.role==NpcRole::Ship && id!=pilot &&
                   service.world().npc_available_for_duty(id))
                    parents.push_back(id);
            CHECK(parents.size()>=2);
            const auto guardian=parents[0],coparent=parents[1];
            constexpr std::uint64_t seed=943061;
            const std::string family_scope="wo3-full-household";
            const std::string region_scope="wo3-full-region";
            std::uint64_t original_ship_count=0;
            {
                NpcAppearanceStore registry(service_db,family_scope,seed,
                    [&](std::uint64_t id){
                        return service.world().npc_records().count(id)!=0;
                    });
                for(const auto& [id,n]:service.world().npc_records()){
                    (void)id;
                    CHECK(n.legal_residence().id==ship);
                    ++original_ship_count;
                }
                registry.register_settlement(ship,original_ship_count);
                registry.register_settlement(surface,0); // no invented settler
                for(const auto& [id,n]:service.world().npc_records()){
                    (void)n;
                    registry.enroll_family_founder(id,ship);
                }
            }
            namespace regional=galaxy::regional_events;
            regional::CheckpointStore region(service_db,region_scope);
            regional::Scheduler scheduler;
            cold_colony::State home{},empty_surface{};
            home.population=original_ship_count;
            empty_surface.population=0;
            scheduler.add_colony("home",home);
            scheduler.add_colony("surface",empty_surface);
            CHECK(region.save(scheduler,0)==1);
            regional::RegionalModeAuthority authority(region);
            CHECK(authority.enroll().generation==1);
            const auto child=(std::uint64_t{1}<<62)+85001;
            SocietyBirthPlan born{};
            born.expected_world_generation=service.generation();
            born.expected_region_revision=1;
            born.expected_owner_generation=1;
            born.family_seed=seed;born.expected_family_revision=0;
            born.event_id=85001;born.child_id=child;
            born.parent_a=guardian;born.parent_b=coparent;
            born.home_ship=ship;
            born.family_scope=family_scope;born.region_scope=region_scope;
            born.regional_colony="home";
            service.step_with_atomic_dependent_birth(born);
            Command registration{};
            registration.id=85002;registration.sequence=85002;
            registration.issuer=Simulation::kServerAuthority;
            registration.actor=ship;registration.target=ship;
            registration.action=Action::HostRegisterDependent;
            registration.transport_id=child;
            registration.passenger_ids={guardian};
            registration.execute_tick=service.world().tick()+1;
            CHECK(service.submit_host_dependent_registration(
                registration,family_scope,seed));
            service.step_with_dependent_registration();
            CHECK(service.book_planetary_shuttle_family(
                7,ship,key,guardian,coparent,{child},85110));
            CHECK(service.load_planetary_shuttle(
                7,ship,key,ShuttleCargo::Water,10));
            down(service,ship,key,85111);
            CHECK(service.world().physical_dependents().at(child).location==
                  surface);
            // Two actual source workers and three arriving family members
            // require the finished annex; the original 4-bed house is full.
            CHECK(service.world().planetary_sites().at(key).
                  service_projects[0].phase==2);
            service.steps(20); // original solar + finite vital support ticks
            PlanetaryHouseholdSettlementPlan admission{};
            admission.expected_world_generation=service.generation();
            admission.request_id=85520;
            admission.coparent_move_id=85521;
            admission.child_move_id=85522;
            admission.expected_region_revision=region.load()->revision;
            admission.expected_owner_generation=authority.state().generation;
            admission.expected_origin_family_revision=1;
            admission.expected_destination_family_revision=0;
            admission.family_seed=seed;
            admission.owner=7;admission.ship=ship;admission.site=key;
            admission.guardian_id=guardian;
            admission.guardian_consented=true;
            admission.coparent_consented=true;
            admission.family_scope=family_scope;
            admission.region_scope=region_scope;
            admission.origin_colony="home";
            admission.destination_colony="surface";
            auto spoofed_household=admission;
            spoofed_household.owner=8;
            CHECK(!service.commit_planetary_household_settlement(
                spoofed_household));
            bool failed_admission=false;
            const auto before_admission=service.world().state_hash();
            try {
                (void)service.commit_planetary_household_settlement(
                    admission,FailPoint::AfterSnapshotWrite);
            }catch(const std::exception&){failed_admission=true;}
            CHECK(failed_admission &&
                  service.world().state_hash()==before_admission);
            CHECK(service.commit_planetary_household_settlement(admission));
            CHECK(service.world().planetary_sites().at(key).
                  households.count(guardian)==1);
            CHECK(service.world().npc_records().at(guardian).legal_residence()==
                  (transport::Endpoint{transport::LocationKind::PlanetarySite,
                                       surface}));
            CHECK(service.world().physical_dependents().at(child).location==
                  surface);
            CHECK(region.load()->scheduler.colony("surface").population==3);
            const auto audit_resident=colony03::audit_planetary_surface(
                service.world(),7,ship,key);
            CHECK(audit_resident && audit_resident->admitted_households==1 &&
                  audit_resident->present_registered_children==1 &&
                  audit_resident->installed_beds==8);
            const auto admitted_copy=(folder/"household_admitted.sqlite").string();
            PersistentWorld::recover_copy(service_db,admitted_copy);
            {
                PersistentWorld copy(admitted_copy,Simulation(945));
                CHECK(copy.world().state_hash()==service.world().state_hash());
            }
            CHECK(service.book_planetary_household_return(
                7,ship,key,guardian,85530));
            CHECK(service.load_planetary_shuttle(
                7,ship,key,ShuttleCargo::Water,10));
            up(service,ship,key,85531);
            CHECK(service.world().planetary_shuttles().at(key).family.stage==7);
            PlanetaryHouseholdSettlementPlan departure=admission;
            departure.expected_world_generation=service.generation();
            departure.request_id=85536;
            departure.coparent_move_id=85537;
            departure.child_move_id=85538;
            departure.returning=true;
            departure.expected_region_revision=region.load()->revision;
            departure.expected_origin_family_revision=3;
            departure.expected_destination_family_revision=4;
            departure.origin_colony="surface";
            departure.destination_colony="home";
            const auto before_departure=service.world().state_hash();
            bool failed_return=false;
            try {
                (void)service.commit_planetary_household_settlement(
                    departure,FailPoint::AfterSnapshotWrite);
            }catch(const std::exception&){failed_return=true;}
            CHECK(failed_return &&
                  service.world().state_hash()==before_departure);
            CHECK(service.commit_planetary_household_settlement(departure));
            CHECK(service.world().npc_records().at(guardian).legal_residence()==
                  (transport::Endpoint{transport::LocationKind::ColonyShip,ship}));
            CHECK(service.world().physical_dependents().at(child).location==ship);
            CHECK(service.world().planetary_shuttles().at(key).family.stage==0);
            CHECK(service.world().planetary_shuttles().at(key).passenger_ids.empty());
            CHECK(service.world().planetary_sites().at(key).
                  households.at(guardian).return_request==85536);
            // A saved household must not be forgeable by replacing the
            // consent/return request; restore cross-checks the actual two
            // parents, original physical child and immutable receipts.
            auto forged_household=service.world().canonical_state();
            const auto household_line=forged_household.find("planethousehold|");
            CHECK(household_line!=std::string::npos);
            const auto return_field=forged_household.find("|85536|",household_line);
            CHECK(return_field!=std::string::npos);
            forged_household.replace(return_field,7,"|0|");
            bool household_tamper_denied=false;
            try{(void)Simulation::from_snapshot(forged_household);}
            catch(const std::exception&){household_tamper_denied=true;}
            CHECK(household_tamper_denied);
            CHECK(region.load()->scheduler.colony("surface").population==0);
            CHECK(region.load()->scheduler.colony("home").population==
                  original_ship_count+1);
            CHECK(!service.commit_planetary_household_settlement(departure));
            CHECK(service.world().total_ore()==mass_before);
            CHECK(Simulation::from_snapshot(service.world().canonical_state()).
                  state_hash()==service.world().state_hash());
            const auto returned_copy=(folder/"household_returned.sqlite").string();
            PersistentWorld::recover_copy(service_db,returned_copy);
            {
                PersistentWorld verified(returned_copy,Simulation(946));
                CHECK(verified.world().state_hash()==service.world().state_hash());
                CHECK(verified.world().total_ore()==mass_before);
            }
            // WO3-F: a valid v118 household is recoverable, but deleting
            // only its atomic root receipt must never let the three ART02
            // family moves or original regional population masquerade as
            // an independently accepted colony migration.
            const auto forged_copy=(folder/"household_root_removed.sqlite").string();
            PersistentWorld::recover_copy(service_db,forged_copy);
            sqlite3* raw=nullptr;
            CHECK(sqlite3_open(forged_copy.c_str(),&raw)==SQLITE_OK);
            const auto deleted=sqlite3_exec(raw,
                "DELETE FROM sg_planet_household_migration "
                "WHERE request_id='85520'",nullptr,nullptr,nullptr);
            CHECK(sqlite3_close(raw)==SQLITE_OK);
            CHECK(deleted==SQLITE_OK);
            bool rejected_missing_root=false;
            try{PersistentWorld forged(forged_copy,Simulation(947));}
            catch(const std::exception&){rejected_missing_root=true;}
            CHECK(rejected_missing_root);
            // A retained root with a forged regional cross-store effect
            // must also fail independent copied SQLite restoration.
            const auto forged_region=(folder/"household_region_forged.sqlite").string();
            PersistentWorld::recover_copy(service_db,forged_region);
            raw=nullptr;
            CHECK(sqlite3_open(forged_region.c_str(),&raw)==SQLITE_OK);
            const auto changed=sqlite3_exec(raw,
                "UPDATE sg_regional_command_receipt SET outcome=0 "
                "WHERE scope='wo3-full-region' AND command_id='85536'",
                nullptr,nullptr,nullptr);
            CHECK(sqlite3_close(raw)==SQLITE_OK);
            CHECK(changed==SQLITE_OK);
            bool rejected_forged_region=false;
            try{PersistentWorld forged(forged_region,Simulation(948));}
            catch(const std::exception&){rejected_forged_region=true;}
            CHECK(rejected_forged_region);
        }
    }
    const auto mass_before_production=w.world().total_ore();
    CHECK(mass_before_production==initial_mass);
    bool rolled_back=false;
    try {
        (void)w.start_planetary_production_build(
            7,ship,key,true,81800,builder,FailPoint::AfterSnapshotWrite);
    }catch(const std::exception&){rolled_back=true;}
    CHECK(rolled_back);
    CHECK(w.world().planetary_sites().at(key).production_projects[0].phase==0);
    // A real original shuttle visitor with three native Shape credits
    // must hold temporary site duty; presence alone does not authorize work.
    CHECK(w.assign_planetary_shuttle_visitor_duty(
        7,ship,key,trained_visitor,false));
    CHECK(!w.start_planetary_production_build(
        7,ship,key,true,81800,trained_visitor));
    CHECK(w.assign_planetary_shuttle_visitor_duty(
        7,ship,key,trained_visitor,true));
    CHECK(w.start_planetary_production_build(
        7,ship,key,true,81800,trained_visitor));
    const auto& begun=w.world().planetary_sites().at(key);
    CHECK(begun.production_projects[0].phase==1 &&
          begun.production_projects[0].progress==0 &&
          begun.production_projects[0].supervisor==trained_visitor &&
          begun.housing_modules==0 && !begun.farm &&
          begun.embodied_ore==before_production_embodied+4);
    CHECK(w.world().total_ore()==initial_mass);
    const auto pending_farm=colony03::audit_planetary_surface(
        w.world(),7,ship,key);
    CHECK(pending_farm && pending_farm->production_phases[0]==1 &&
          pending_farm->production_progress[0]==0 &&
          pending_farm->production_order_ids[0]==81800 &&
          pending_farm->production_phases[1]==0);
    CHECK(w.world().canonical_state().rfind("galaxy-kernel-v113|",0)==0);
    CHECK(Simulation::from_snapshot(w.world().canonical_state()).state_hash()==
          w.world().state_hash());
    const auto mid_copy=(folder/"production_midbuild.sqlite").string();
    PersistentWorld::recover_copy(db,mid_copy);
    PersistentWorld twin(mid_copy,Simulation(930));
    CHECK(twin.world().state_hash()==w.world().state_hash());
    w.steps(12);twin.steps(12);
    CHECK(twin.world().state_hash()==w.world().state_hash());
    CHECK(w.pause_planetary_production_build(7,ship,key,true,true));
    CHECK(twin.pause_planetary_production_build(7,ship,key,true,true));
    const auto frozen=w.world().planetary_sites().at(key).production_projects[0].progress;
    w.steps(8);twin.steps(8);
    CHECK(w.world().planetary_sites().at(key).production_projects[0].progress==frozen);
    CHECK(twin.world().state_hash()==w.world().state_hash());
    CHECK(w.pause_planetary_production_build(7,ship,key,true,false));
    CHECK(twin.pause_planetary_production_build(7,ship,key,true,false));
    CHECK(w.assign_planetary_shuttle_visitor_duty(
        7,ship,key,trained_visitor,false));
    CHECK(twin.assign_planetary_shuttle_visitor_duty(
        7,ship,key,trained_visitor,false));
    w.steps(5);twin.steps(5);
    CHECK(w.world().planetary_sites().at(key).production_projects[0].progress==frozen);
    CHECK(w.world().planetary_sites().at(key).production_projects[0].stalled_ticks>=5);
    CHECK(twin.world().state_hash()==w.world().state_hash());
    // Physical visiting supervisor became unavailable. Replace it with
    // another genuinely certified original delegate while retaining all
    // accumulated work, deposited materials and the same job order.
    CHECK(!w.handover_planetary_production_build(
        8,ship,key,true,trained_visitor,builder,81900));
    CHECK(!w.handover_planetary_production_build(
        7,ship,key,true,pilot,builder,81900));
    bool handover_failed=false;
    try {
        (void)w.handover_planetary_production_build(
            7,ship,key,true,trained_visitor,builder,81900,
            FailPoint::AfterSnapshotWrite);
    }catch(const std::exception&){handover_failed=true;}
    CHECK(handover_failed);
    CHECK(w.world().planetary_sites().at(key).production_projects[0].supervisor==
          trained_visitor);
    CHECK(w.handover_planetary_production_build(
        7,ship,key,true,trained_visitor,builder,81900));
    CHECK(twin.handover_planetary_production_build(
        7,ship,key,true,trained_visitor,builder,81900));
    CHECK(!w.handover_planetary_production_build(
        7,ship,key,true,trained_visitor,builder,81900));
    const auto handed=w.world().planetary_sites().at(key);
    CHECK(handed.production_projects[0].supervisor==builder &&
          handed.production_projects[0].progress==frozen &&
          handed.production_projects[0].order_id==81800 &&
          handed.production_handovers[0].size()==1 &&
          handed.production_handovers[0][0].request_id==81900 &&
          handed.production_handovers[0][0].previous==trained_visitor &&
          handed.production_handovers[0][0].replacement==builder);
    const auto changed_supervisor=colony03::audit_planetary_surface(
        w.world(),7,ship,key);
    CHECK(changed_supervisor &&
          changed_supervisor->production_phases[0]==1 &&
          changed_supervisor->production_progress[0]==frozen &&
          changed_supervisor->production_order_ids[0]==81800 &&
          changed_supervisor->production_handover_count[0]==1 &&
          changed_supervisor->production_last_handover_id[0]==81900);
    CHECK(w.world().canonical_state().rfind("galaxy-kernel-v114|",0)==0);
    CHECK(w.world().state_hash()==twin.world().state_hash());
    // Inactive visitor duty never resumes; the real replacement supervises.
    w.steps(28);twin.steps(28);
    CHECK(w.world().state_hash()==twin.world().state_hash());
    const auto& finished=w.world().planetary_sites().at(key);
    CHECK(finished.production_projects[0].phase==2 &&
          finished.production_projects[0].progress==40 &&
          finished.production_projects[0].completed_tick>0 &&
          finished.farm && finished.extractor==false);
    CHECK(w.world().total_ore()==initial_mass);
    CHECK(!w.start_planetary_production_build(
        7,ship,key,true,81801,builder)); // installed, not a second farm
    Command repeat_production{};
    repeat_production.id=81800;
    repeat_production.sequence=81800;
    repeat_production.execute_tick=w.world().tick()+1;
    repeat_production.issuer=7;
    repeat_production.actor=ship;
    repeat_production.action=Action::Thrust;
    CHECK(!w.submit(repeat_production)); // original project ID remains owned
    repeat_production.id=81900;
    repeat_production.sequence=81900;
    CHECK(!w.submit(repeat_production)); // replacement approval immutable
    const auto completed_snapshot=w.world().canonical_state();
    auto forged_production=completed_snapshot;
    const auto production_row=forged_production.find("planetproduction|");
    CHECK(production_row!=std::string::npos);
    const auto production_end=forged_production.find('\n',production_row);
    const auto final_field=forged_production.rfind('|',production_end);
    CHECK(final_field!=std::string::npos);
    forged_production.replace(final_field+1,
        production_end-final_field-1,"1"); // completed project cannot be paused
    bool rejects_tampering=false;
    try {(void)Simulation::from_snapshot(forged_production);}
    catch(const std::exception&){rejects_tampering=true;}
    CHECK(rejects_tampering);
    auto forged_succession=completed_snapshot;
    const auto relief=forged_succession.find("planetproductionhandover|");
    CHECK(relief!=std::string::npos);
    const auto relief_end=forged_succession.find('\n',relief);
    const auto relief_last=forged_succession.rfind('|',relief_end);
    CHECK(relief_last!=std::string::npos);
    forged_succession.replace(relief_last+1,
        relief_end-relief_last-1,"999999");
    bool refuses_succession_forgery=false;
    try {(void)Simulation::from_snapshot(forged_succession);}
    catch(const std::exception&){refuses_succession_forgery=true;}
    CHECK(refuses_succession_forgery);
    CHECK(Simulation::from_snapshot(completed_snapshot).canonical_state()==
          completed_snapshot);
    const auto completed_copy=(folder/"production_finished.sqlite").string();
    PersistentWorld::recover_copy(db,completed_copy);
    PersistentWorld completed(completed_copy,Simulation(931));
    CHECK(completed.world().state_hash()==w.world().state_hash());
    CHECK(completed.world().total_ore()==initial_mass);
    std::cout<<"WO3-C/D original qualified utility and physical farm construction/SQLite PASS\n";
    return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
