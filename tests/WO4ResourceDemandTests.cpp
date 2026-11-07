#include "galaxy/WO4ResourceDemand.hpp"
#include "galaxy/WO4Allocation.hpp"
#include "galaxy/WO4LocalAllocation.hpp"
#include "galaxy/WO4ConsumptionProfiles.hpp"
#include "galaxy/WO4RegionalDistribution.hpp"
#include "galaxy/Persistence.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do { if(!(x)) throw std::runtime_error( \
    "WO4-A original native line "+std::to_string(__LINE__)+": " #x); } while(0)
static void applied(PersistentWorld& w,EntityId ship,std::uint64_t id,
                    Action action,EntityId target=0,std::int64_t amount=1,
                    int selector=0){
    Command c{};c.id=id;c.sequence=id;c.issuer=7;c.actor=ship;
    c.target=target;c.action=action;c.amount=amount;
    c.thrust_x=selector;c.execute_tick=w.world().tick()+1;
    CHECK(w.submit(c));w.step();
    CHECK(!w.world().events().empty());
    CHECK(w.world().events().back().command_id==id &&
          w.world().events().back().result==Outcome::Applied);
}
int main(){
 try {
    using namespace economy04;
    // Pure forecasts over already held kg: never create stock by planning.
    const auto pressured=calculate(2,{1,3,11},Policy{4,1});
    CHECK(pressured.has_value());
    CHECK((*pressured)[0].vital==Vital::Food &&
          (*pressured)[0].pressure==Pressure::CriticalCurrentWindow &&
          (*pressured)[0].reserve_target_kg==8 &&
          (*pressured)[0].reserve_deficit_kg==7);
    CHECK((*pressured)[1].pressure==Pressure::BelowReserve &&
          (*pressured)[1].complete_windows_covered==1);
    CHECK((*pressured)[2].pressure==Pressure::MeetsReserve &&
          (*pressured)[2].buffer_above_target_kg==3);
    const auto empty=calculate(0,{50,60,70});
    CHECK(empty.has_value());
    for(const auto& need:*empty)
        CHECK(need.pressure==Pressure::NoCurrentConsumers &&
              need.reserve_deficit_kg==0 &&
              need.buffer_above_target_kg==0);
    CHECK(!calculate(1,{0,-1,0}));
    CHECK(!calculate(1,{0,0,0},Policy{0,1}));
    CHECK(!calculate(1,{0,0,0},Policy{1,0}));
    CHECK(!calculate(std::numeric_limits<std::uint64_t>::max(),
                     {0,0,0},Policy{4,1}));
    CHECK(!calculate(2,{0,0,0},Policy{33,1}));

    const auto folder=fs::temp_directory_path()/
        ("wo4_demand_"+std::to_string(std::chrono::steady_clock::now().
                                     time_since_epoch().count()));
    fs::create_directories(folder);
    struct Cleanup {fs::path path;~Cleanup(){
        std::error_code ec;fs::remove_all(path,ec);
    }} cleanup{folder};
    const auto db=(folder/"original.sqlite").string();
    Simulation original(47004);
    const auto ship=original.add_colony_ship(7,{0,0},200,8);
    original.provision_colony_genesis(ship,1000,3000,1000,1000,1000);
    const auto ore=original.add_asteroid({2000,0},120);
    original.enable_individual_workforce();
    const auto starting_mass=original.total_material_kg();
    PersistentWorld w(db,std::move(original));
    CHECK(!observe_owner_world(w.world(),8,ship));
    CHECK(!observe_owner_world(w.world(),7,999999));
    const auto baseline_hash=w.world().state_hash();
    const auto baseline=observe_owner_world(w.world(),7,ship);
    CHECK(baseline.has_value() &&
          baseline->compartments.size()==1 &&
          baseline->still_current(w.world()));
    const auto& onboard=baseline->compartments[0];
    CHECK(onboard.kind==CompartmentKind::ColonyShip);
    CHECK(onboard.physical_location==ship &&
          onboard.current_consumers==
             static_cast<std::uint64_t>(w.world().colony_ship(ship)->crew));
    CHECK(onboard.needs[0].physically_present_kg==
          w.world().life_support(ship)->food_kg);
    CHECK(onboard.needs[1].physically_present_kg==
          w.world().life_support(ship)->water_kg);
    CHECK(onboard.needs[2].physically_present_kg==
          w.world().life_support(ship)->oxygen_kg);
    CHECK(w.world().state_hash()==baseline_hash);
    CHECK(w.world().total_material_kg()==starting_mass);

    // Real construction changes the authoritative world. Merely observing
    // it does not transfer any provisions from the ship to the habitat.
    applied(w,ship,1,Action::Mine,ore,80);
    applied(w,ship,2,Action::RefineOre,0,80);
    applied(w,ship,3,Action::FabricateModule,0,25);
    applied(w,ship,4,Action::ConstructOutpost);
    CHECK(w.world().built_outposts().size()==1);
    const auto site=w.world().built_outposts().begin()->first;
    applied(w,ship,5,Action::ConstructHabitat,site);
    const auto new_hash=w.world().state_hash();
    CHECK(!baseline->still_current(w.world()));
    const auto built=observe_owner_world(w.world(),7,ship);
    CHECK(built && built->compartments.size()==2);
    const auto& awaiting=built->compartments[1];
    CHECK(awaiting.kind==CompartmentKind::OutpostHabitat &&
          awaiting.physical_location==site &&
          awaiting.current_consumers==0);
    for(const auto& need:awaiting.needs)
        CHECK(need.pressure==Pressure::NoCurrentConsumers);
    CHECK(w.world().state_hash()==new_hash);

    // Approved original stock transfers and real NPC resident assignment
    // can change the forecast, never the other way round.
    applied(w,ship,6,Action::TransferHabitatStock,site,6,1);
    applied(w,ship,7,Action::TransferHabitatStock,site,6,2);
    applied(w,ship,8,Action::TransferHabitatStock,site,6,3);
    applied(w,ship,9,Action::AssignHabitatResidents,site,2);
    const auto observed=observe_owner_world(w.world(),7,ship,Policy{4,1});
    CHECK(observed && observed->compartments.size()==2);
    const auto& resident=observed->compartments[1];
    CHECK(resident.current_consumers==2);
    CHECK(resident.needs[0].physically_present_kg==
          w.world().habitat(site)->life.food_kg);
    CHECK(resident.needs[0].reserve_target_kg==8);
    CHECK(resident.needs[0].pressure==Pressure::BelowReserve);
    CHECK(resident.needs[0].reserve_deficit_kg==2);
    CHECK(resident.needs[0].buffer_above_target_kg==0);
    CHECK(!observe_owner_world(w.world(),7,ship,Policy{0,1}));
    const auto settled_hash=w.world().state_hash();
    CHECK(observed->still_current(w.world()));
    CHECK(w.world().state_hash()==settled_hash &&
          w.world().total_material_kg()==starting_mass);

    // WO4-B priority policy protects the original ship's own four-window
    // reserve before proposing a single kg for its actual built outpost.
    const AllocationPolicy allocation_policy{Policy{4,1},true,true};
    CHECK(!plan_local_vitals(*observed,w.world(),AllocationPolicy{}));
    const auto planned=plan_local_vitals(*observed,w.world(),allocation_policy);
    CHECK(planned.has_value() && planned->still_current(w.world()));
    CHECK(planned->grants.size()==3);
    for(std::size_t i=0;i<planned->grants.size();++i) {
        const auto& g=planned->grants[i];
        CHECK(g.destination_kind==CompartmentKind::OutpostHabitat &&
              g.destination_location==site && g.source_ship==ship &&
              g.quantity_kg==2 && g.scope==TransferScope::OriginalLocalCommand &&
              g.priority==Priority::VitalReserve &&
              g.vital==static_cast<Vital>(i));
    }
    // WO4-C: a read-only offer does not authorize material movement.
    // No approval, forged quantity or stale state may submit a native command.
    const auto proposal_hash=w.world().state_hash();
    CHECK(commit_owner_approved_local_vital(
        w,planned->grants[0],allocation_policy,10,false).status==
        CommitStatus::Refused);
    auto forged=planned->grants[0];++forged.quantity_kg;
    CHECK(commit_owner_approved_local_vital(
        w,forged,allocation_policy,10,true).status==CommitStatus::Refused);
    CHECK(w.world().state_hash()==proposal_hash);
    const auto executed=commit_owner_approved_local_vital(
        w,planned->grants[0],allocation_policy,10,true);
    CHECK(executed.status==CommitStatus::Applied &&
          executed.native_outcome==Outcome::Applied &&
          executed.native_transferred_kg==2);
    CHECK(w.world().has_command_id(10));
    CHECK(w.world().habitat(site)->life.food_kg==8);
    CHECK(!planned->still_current(w.world()));
    CHECK(commit_owner_approved_local_vital(
        w,planned->grants[1],allocation_policy,11,true).status==
        CommitStatus::Refused);
    CHECK(commit_owner_approved_local_vital(
        w,planned->grants[0],allocation_policy,10,true).status==
        CommitStatus::Refused);
    const auto post=observe_owner_world(w.world(),7,ship,Policy{4,1});
    CHECK(post && post->compartments.size()==2);
    CHECK(post->compartments[1].needs[0].pressure==Pressure::MeetsReserve);
    CHECK(post->compartments[1].needs[1].reserve_deficit_kg==2);
    CHECK(w.world().total_material_kg()==starting_mass);
    // WO4-D derives actual 20-tick native ship/habitat rations without
    // consuming or producing any goods. Durable chairs are not per-tick food.
    const auto cycles=observe_native_need_cycles(w.world(),*post);
    CHECK(cycles && cycles->still_current(w.world()));
    CHECK(cycles->cycles.size()==2);
    CHECK(cycles->cycles[0].cadence_ticks==20 &&
          cycles->cycles[0].persons==post->compartments[0].current_consumers);
    CHECK(cycles->cycles[1].cadence_ticks==20 &&
          cycles->cycles[1].persons==2);
    CHECK(cycles->cycles[1].per_due_kg_each_vital==2 &&
          !cycles->cycles[1].conditional);
    CHECK(!describe_good("item/not_real"));
    const auto chair=describe_good("item/chair");
    const auto meal=describe_good("item/meal");
    CHECK(chair && chair->role==GoodsRole::Durable &&
          !chair->automatically_consumed_by_original_world);
    CHECK(meal && meal->role==GoodsRole::Consumable &&
          !meal->automatically_consumed_by_original_world);
    const auto medicine=describe_service("service/clinical_care");
    CHECK(medicine && !medicine->native_executable &&
          !medicine->consumables.empty() &&
          !medicine->equipment.empty());
    CHECK(!describe_service("service/invented_care"));

    // WO4-E observes separately owned *original physical locations*, not
    // imaginary regional stock pools. No carrier route or dispatch is
    // created by a resource request.
    Simulation regional(47006);
    const auto provider=regional.add_colony_ship(7,{0,0},100,10);
    regional.provision_colony_genesis(provider,1000,1000,1000,1000,1000);
    const auto consumer=regional.add_colony_ship(7,{20000,0},100,30);
    regional.provision_colony_genesis(consumer,100,100,100,300,400);
    const auto region_hash=regional.state_hash();
    const auto board=observe_regional_distribution(regional,7,Policy{4,1});
    CHECK(board && board->still_current(regional));
    CHECK(board->shortages.size()==3);
    CHECK(board->unbooked_carrier_requests.size()==3);
    for(std::size_t i=0;i<3;++i) {
        const auto& req=board->unbooked_carrier_requests[i];
        CHECK(req.source.source_ship==provider &&
              req.source.physical_location==provider &&
              req.destination.source_ship==consumer &&
              req.destination.physical_location==consumer &&
              req.proposed_kg==20 &&
              req.vital==static_cast<Vital>(i) &&
              req.priority==Priority::VitalReserve &&
              req.requires_original_carrier_and_authorization);
    }
    CHECK(!observe_regional_distribution(regional,8));
    CHECK(regional.state_hash()==region_hash &&
          board->unbooked_carrier_requests[0].observed_state_hash==region_hash);
    // Local same-ship habitat transfers are never presented as remote
    // carrier receipts, which would double-book the ship's provisions.
    const auto local_board=observe_regional_distribution(
        w.world(),7,Policy{4,1});
    CHECK(local_board && local_board->still_current(w.world()));
    CHECK(local_board->unbooked_carrier_requests.empty());

    // Independent original SQLite copy: same physical inventory and
    // population imply the same derived demand lines on restart.
    const auto copied=(folder/"restored.sqlite").string();
    PersistentWorld::recover_copy(db,copied);
    PersistentWorld recovered(copied,Simulation(47005));
    CHECK(recovered.world().state_hash()==w.world().state_hash());
    const auto replay=observe_owner_world(recovered.world(),7,ship,Policy{4,1});
    CHECK(replay && replay->still_current(recovered.world()) &&
          replay->compartments.size()==post->compartments.size());
    for(std::size_t i=0;i<replay->compartments.size();++i){
        const auto& a=replay->compartments[i];
        const auto& b=post->compartments[i];
        CHECK(a.kind==b.kind && a.current_consumers==b.current_consumers &&
              a.physical_location==b.physical_location);
        for(std::size_t j=0;j<a.needs.size();++j){
            CHECK(a.needs[j].reserve_target_kg==b.needs[j].reserve_target_kg);
            CHECK(a.needs[j].physically_present_kg==b.needs[j].physically_present_kg);
            CHECK(a.needs[j].reserve_deficit_kg==b.needs[j].reserve_deficit_kg);
        }
    }
    CHECK(recovered.world().total_material_kg()==starting_mass);
    const auto reloaded_cycles=observe_native_need_cycles(
        recovered.world(),*replay);
    CHECK(reloaded_cycles && reloaded_cycles->cycles.size()==cycles->cycles.size());
    CHECK(recovered.world().has_command_id(10)); // original receipt not WO4 debt
    CHECK(commit_owner_approved_local_vital(
        recovered,planned->grants[0],allocation_policy,10,true).status==
        CommitStatus::Refused);
    std::cout<<"WO4-A-E original demand, priority, native allocation, profiles, region and copied SQLite: OK\n";
    return 0;
 }catch(const std::exception& e){
    std::cerr<<e.what()<<'\n';
    return 1;
 }
}
