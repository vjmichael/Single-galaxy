#include "galaxy/WO3ColonyOperations.hpp"
#include "galaxy/WO2IndustryWorkforce.hpp"
#include "galaxy/Persistence.hpp"
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
    const auto mass_before_production=w.world().total_ore();
    CHECK(mass_before_production==initial_mass);
    bool rolled_back=false;
    try {
        (void)w.start_planetary_production_build(
            7,ship,key,true,81800,builder,FailPoint::AfterSnapshotWrite);
    }catch(const std::exception&){rolled_back=true;}
    CHECK(rolled_back);
    CHECK(w.world().planetary_sites().at(key).production_projects[0].phase==0);
    CHECK(w.start_planetary_production_build(7,ship,key,true,81800,builder));
    const auto& begun=w.world().planetary_sites().at(key);
    CHECK(begun.production_projects[0].phase==1 &&
          begun.production_projects[0].progress==0 &&
          begun.production_projects[0].supervisor==builder &&
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
