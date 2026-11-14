#include "galaxy/Stage07ResidentEconomy.hpp"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do {if(!(x)) throw std::runtime_error( \
    std::string("Stage07 POP08 line ")+std::to_string(__LINE__)+": " #x);}while(0)

static void native(PersistentWorld& w,EntityId ship,std::uint64_t id,
                   Action action,EntityId target=0,
                   std::int64_t amount=1,int setting=0) {
    Command cmd{};
    cmd.id=id;cmd.sequence=id;cmd.execute_tick=w.world().tick()+1;
    cmd.issuer=7;cmd.actor=ship;cmd.target=target;
    cmd.action=action;cmd.amount=amount;cmd.thrust_x=setting;
    CHECK(w.submit(cmd));
    w.step();
    CHECK(w.world().events().back().command_id==id);
    CHECK(w.world().events().back().result==Outcome::Applied);
}
static std::uint64_t aboard(const Simulation& sim,EntityId ship,
                            std::uint64_t excluded=0) {
    for(const auto& [id,p]:sim.npc_records())
        if(id!=excluded && stage7::authentic_resident(
            sim,7,ship,id,ship))return id;
    return 0;
}

int main(){
  const auto folder=fs::temp_directory_path()/
    ("stage07_pop08_"+std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
  try {
    fs::create_directories(folder);
    struct Cleanup{fs::path path;~Cleanup(){
        std::error_code error;fs::remove_all(path,error);
    }} cleanup{folder};
    // I. A named original resident makes a real supply decision; the native
    // WO4 command, not our decision, transfers exactly two kilograms.
    const auto path=(folder/"commerce.sqlite").string();
    EntityId ship=0,site=0;
    std::uint64_t actor=0,generation=0;
    std::int64_t mass=0;
    {
        Simulation seed(47008);
        ship=seed.add_colony_ship(7,{0,0},200,8);
        seed.provision_colony_genesis(ship,1000,3000,1000,1000,1000);
        const auto ore=seed.add_asteroid({2000,0},120);
        seed.enable_individual_workforce();
        PersistentWorld world(path,std::move(seed));
        native(world,ship,1,Action::Mine,ore,80);
        native(world,ship,2,Action::RefineOre,0,80);
        native(world,ship,3,Action::FabricateModule,0,25);
        native(world,ship,4,Action::ConstructOutpost);
        site=world.world().built_outposts().begin()->first;
        native(world,ship,5,Action::ConstructHabitat,site);
        native(world,ship,6,Action::TransferHabitatStock,site,6,1);
        native(world,ship,7,Action::TransferHabitatStock,site,6,2);
        native(world,ship,8,Action::TransferHabitatStock,site,6,3);
        native(world,ship,9,Action::AssignHabitatResidents,site,2);
        actor=aboard(world.world(),ship);
        CHECK(actor);
        const auto before=world.world().habitat(site)->life.food_kg;
        const auto initial_mass=world.world().total_material_kg();
        const economy04::AllocationPolicy rule{{4,1},true,true};
        const auto proposed=stage7::decide_resident_vital(
            world,7,ship,actor,rule);
        CHECK(proposed && proposed->resident==actor &&
              proposed->original_grant.destination_location==site &&
              proposed->original_grant.vital==economy04::Vital::Food &&
              proposed->original_grant.quantity_kg==2);
        CHECK(!stage7::decide_resident_vital(
            world,8,ship,actor,rule));
        CHECK(stage7::execute_resident_vital(
            world,*proposed,7,90001,false).status==
            economy04::CommitStatus::Refused);
        CHECK(stage7::execute_resident_vital(
            world,*proposed,8,90001,true).status==
            economy04::CommitStatus::Refused);
        CHECK(world.world().habitat(site)->life.food_kg==before);
        // An outsider and a stale/forged original offer cannot move stock.
        auto forged=*proposed;
        ++forged.original_grant.quantity_kg;
        CHECK(stage7::execute_resident_vital(
            world,forged,7,90001,true).status==
            economy04::CommitStatus::Refused);
        const auto result=stage7::execute_resident_vital(
            world,*proposed,7,90001,true);
        CHECK(result.status==economy04::CommitStatus::Applied &&
              result.native_outcome==Outcome::Applied &&
              result.native_transferred_kg==2);
        CHECK(world.world().has_command_id(90001));
        CHECK(world.world().habitat(site)->life.food_kg==before+2);
        CHECK(world.world().total_material_kg()==initial_mass);
        CHECK(stage7::execute_resident_vital(
            world,*proposed,7,90001,true).status==
            economy04::CommitStatus::Refused);
        // A proposed same-owner freight contract cannot replace a real
        // freighter or original planetary pickup. This world has neither.
        CHECK(!stage7::decide_resident_freight(
            world,7,ship,actor,ship,90002));
        CHECK(world.world().local_delivery_board().empty());
        CHECK(world.world().intercolony_freight().empty());
        generation=world.generation();
        mass=world.world().total_material_kg();
    }
    {
        PersistentWorld reopened(path,Simulation(90010));
        CHECK(reopened.generation()==generation &&
              reopened.world().has_command_id(90001));
        CHECK(reopened.world().total_material_kg()==mass);
        CHECK(reopened.world().habitat(site)->life.food_kg==8);
    }
    const auto recovered=(folder/"commerce-recovered.sqlite").string();
    PersistentWorld::recover_copy(path,recovered);
    {
        PersistentWorld copy(recovered,Simulation(90011));
        CHECK(copy.generation()==generation &&
              copy.world().has_command_id(90001));
        CHECK(copy.world().total_material_kg()==mass &&
              copy.world().habitat(site)->life.food_kg==8);
    }

    // II. Only an ORIGINAL recorded citizen social observation AND a
    // measurable food/water reserve deficit can initiate the existing
    // owner's one-use constitutional policy revision. No prosecution.
    const auto legal=(folder/"law.sqlite").string();
    EntityId civic_ship=0;
    std::uint64_t civic_event=0,civic_npc=0,civic_generation=0;
    {
        Simulation seed(47009);
        // The real genesis demands >=100 kg per compartment and an exact
        // 1,000-kg total multiple. Thirty actual adult residents (not
        // synthetic census entries) make 100 kg below the native four-window
        // 120-kg reserve while keeping the colony physically provisioned.
        civic_ship=seed.add_colony_ship(7,{0,0},100,30);
        seed.provision_colony_genesis(civic_ship,100,100,300,200,300);
        seed.enable_individual_workforce();
        seed.upgrade_assignment_snapshot_format();
        seed.enable_passenger_life_support();
        PersistentWorld world(legal,std::move(seed));
        const auto leader=aboard(world.world(),civic_ship);
        CHECK(leader);
        world.establish_colony_conflict_agenda(
            94001,civic_ship,leader,colony_conflict::Agenda::Cooperative);
        CHECK(!stage7::propose_scarcity_review(
            world,8,civic_ship,leader).has_value());
        world.steps(8);
        const auto appraised=stage7::observe_original_society(
            world,7,civic_ship,8);
        CHECK(appraised.observed.observed>0);
        for(const auto& [id,p]:world.world().npc_records()){
            if(id!=leader && stage7::authentic_resident(
                world.world(),7,civic_ship,id,civic_ship) &&
               world.colony_personal_outlook(civic_ship,id)){
                civic_npc=id;break;
            }
        }
        CHECK(civic_npc);
        // An unconstrained shortage claim cannot change law when the actual
        // native compartment meets the caller's stated reserve requirement.
        CHECK(!stage7::propose_scarcity_review(
            world,7,civic_ship,civic_npc,economy04::Policy{1,1}));
        const auto motion=stage7::propose_scarcity_review(
            world,7,civic_ship,civic_npc);
        CHECK(motion && motion->personal_event &&
              motion->observed_npc==civic_npc);
        CHECK(!stage7::approve_scarcity_review(
            world,*motion,7,94002,false));
        CHECK(!stage7::approve_scarcity_review(
            world,*motion,8,94002,true));
        CHECK(world.colony_governance(civic_ship).policy==
              colony_conflict::DiplomaticPolicy::Pragmatic);
        const auto committed=stage7::approve_scarcity_review(
            world,*motion,7,94002,true);
        CHECK(committed && committed->event_id==94002);
        // A review is legal policy, not instant effective change or force.
        CHECK(world.colony_governance(civic_ship).policy==
              colony_conflict::DiplomaticPolicy::Pragmatic);
        world.step();
        CHECK(world.colony_governance(civic_ship).policy==
              colony_conflict::DiplomaticPolicy::Reconciliatory);
        CHECK(!stage7::approve_scarcity_review(
            world,*motion,7,94003,true));
        CHECK(!stage7::propose_scarcity_review(
            world,7,civic_ship,civic_npc));
        civic_event=world.colony_governance(civic_ship).event_id;
        civic_generation=world.generation();
    }
    {
        PersistentWorld reopened(legal,Simulation(90012));
        CHECK(reopened.generation()==civic_generation &&
              reopened.colony_governance(civic_ship).event_id==civic_event &&
              reopened.colony_governance(civic_ship).policy==
                  colony_conflict::DiplomaticPolicy::Reconciliatory &&
              reopened.colony_personal_outlook(civic_ship,civic_npc));
    }
    const auto legal_copy=(folder/"law-recovered.sqlite").string();
    PersistentWorld::recover_copy(legal,legal_copy);
    {
        PersistentWorld copy(legal_copy,Simulation(90013));
        CHECK(copy.generation()==civic_generation &&
              copy.colony_governance(civic_ship).event_id==civic_event &&
              copy.colony_governance(civic_ship).policy==
                  colony_conflict::DiplomaticPolicy::Reconciliatory);
    }
    std::cout<<"Stage 7 POP-08 original named resident, finite supplies, "
                "native receipt, constitutional law and recovered SQLite PASS\n";
    return 0;
  }catch(const std::exception& e){
    std::cerr<<"Stage 7 POP-08 FAIL "<<e.what()<<'\n';
    return 1;
  }
}
