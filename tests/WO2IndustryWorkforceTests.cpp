#include "galaxy/WO2WorkPrograms.hpp"
#include "galaxy/WO2IndustryWorkforce.hpp"
#include "galaxy/Persistence.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error("WO2 native line "+std::to_string(__LINE__)+": " #x);}while(0)
static Event send(PersistentWorld& w,EntityId ship,std::uint64_t id,Action action,
                  EntityId target=0,std::int64_t amount=1,int opt=0,PlayerId owner=7) {
    Command c{};c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
    c.issuer=owner;c.actor=ship;c.target=target;c.action=action;
    c.amount=amount;c.thrust_x=opt;
    CHECK(w.submit(c));
    w.step();return w.world().events().back();
}
static void applied(PersistentWorld& w,EntityId ship,std::uint64_t id,Action a,
                    EntityId target=0,std::int64_t amount=1,int opt=0) {
    CHECK(send(w,ship,id,a,target,amount,opt).result==Outcome::Applied);
}
int main() {
 try {
    workforce02::validate_against_industry01();
    CHECK(workforce02::trades().size()==29);
    CHECK(workforce02::service_roles().size()==9);
    CHECK(workforce02::find_trade("precision_electronics")!=nullptr);
    CHECK(!workforce02::find_trade("precision_electronics")->native_execution);
    CHECK(workforce02::find_trade("cut")->native_execution);
    CHECK(workforce02::find_trade("shape")->native_execution);
    CHECK(workforce02::find_service_role("legal_worker")!=nullptr);
    CHECK(workforce02::find_trade("invented_factory")==nullptr);

    // One finished object expands into actual raw-material and profession
    // prerequisites; the read-only graph cannot create a camera or raw ore.
    const auto camera=workforce02::plan_material_program("item/camera",1);
    CHECK(!camera.steps.empty());
    CHECK(camera.steps.back().recipe_id=="assemble_camera");
    CHECK(camera.steps.back().profession=="electronics_technician");
    CHECK(camera.required_source_items.count("raw/quartz_feed")>0);
    CHECK(camera.required_source_items.count("raw/copper_ore")>0);
    CHECK(camera.sum_worker_ticks>0 && !camera.all_processes_native);
    const auto farm_meal=workforce02::plan_material_program("item/meal",1);
    CHECK(farm_meal.required_source_items.count("raw/harvested_grain")>0);
    CHECK(farm_meal.required_source_items.count("raw/harvested_vegetables")>0);
    const auto native=workforce02::plan_material_program("bulk/aluminum_panels",19000);
    CHECK(!native.steps.empty());
    CHECK(native.steps.back().recipe_id=="native_cut_aluminum_panel");
    CHECK(native.steps.back().currently_native_executable);
    CHECK(!native.all_processes_native); // ore still needs original physical mining
    const auto legal=workforce02::describe_service_program("service/legal_consultation");
    CHECK(legal.required_professions.size()==1);
    CHECK(legal.required_professions[0]=="legal_practitioner");
    CHECK(!legal.executable_now);
    bool rejected=false;
    try{(void)workforce02::plan_material_program("item/invented",1);}
    catch(const std::invalid_argument&){rejected=true;}
    CHECK(rejected);


    const auto folder=fs::temp_directory_path()/
       ("wo2_industry_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(folder);
    const auto db=(folder/"world.sqlite").string();
    const auto backup=(folder/"recovered.sqlite").string();
    EntityId ship=0,site=0;
    std::int64_t initial_mass=0;
    std::uint64_t first_id=0,second_id=0;
    {
        Simulation s(92001);
        ship=s.add_colony_ship(7,{0,0},200,8);
        s.provision_colony_genesis(ship,1000,3000,1000,1000,1000);
        const auto bulk=s.add_asteroid({2000,0},150);
        const auto silicate=s.add_geological_asteroid({1000,0},AsteroidClass::Silicate,30,2500);
        s.enable_individual_workforce();
        initial_mass=s.total_material_kg();
        PersistentWorld w(db,std::move(s));
        applied(w,ship,1,Action::Mine,bulk,80);
        applied(w,ship,2,Action::RefineOre,0,80);
        applied(w,ship,3,Action::FabricateModule,0,20);
        applied(w,ship,4,Action::ConstructOutpost);
        site=w.world().built_outposts().begin()->first;
        applied(w,ship,6,Action::ConstructWorkshop,site);

        // Unbuilt finisher cannot magically employ anyone.
        auto before=workforce02::plan_original_workshop(w.world(),7,ship,site);
        CHECK(before.size()==2);
        CHECK(before[0].gate==workforce02::StaffGate::MissingHabitat);
        CHECK(before[1].gate==workforce02::StaffGate::MissingHabitat);
        CHECK(workforce02::plan_original_workshop(w.world(),8,ship,site)[0].gate==
              workforce02::StaffGate::InvalidOwner);
        CHECK(workforce02::plan_original_workshop(w.world(),7,ship,999999)[0].gate==
              workforce02::StaffGate::MissingOriginalSite);
        applied(w,ship,8,Action::ConstructUtility,site);
        applied(w,ship,9,Action::ConstructHabitat,site);
        for(int n=1;n<=3;++n)applied(w,ship,9+n,Action::TransferHabitatStock,site,100,n);
        applied(w,ship,13,Action::AssignHabitatResidents,site,2);
        applied(w,ship,14,Action::AssignUtilityTechnicians,site,1);
        applied(w,ship,15,Action::TransferUtilitySpares,site,5);
        applied(w,ship,16,Action::SetUtilityMaintenance,site,1);
        before=workforce02::plan_original_workshop(w.world(),7,ship,site);
        CHECK(before[0].can_offer_assignment());
        CHECK(before[0].work_state==workforce02::WorkState::NeedsInput ||
              before[0].work_state==workforce02::WorkState::NeedsPower);
        CHECK(before[1].gate==workforce02::StaffGate::FinisherNotConstructed);
        CHECK(before[1].suggested_npc==0);
        CHECK(!workforce02::approved_original_assignment(
            w.world(),7,ship,site,false,before[0].suggested_npc,80,false).has_value());
        CHECK(!workforce02::approved_original_assignment(
            w.world(),8,ship,site,false,before[0].suggested_npc,81,true).has_value());
        CHECK(!workforce02::approved_original_assignment(
            w.world(),7,ship,site,false,999999,82,true).has_value());

        // First real mine and refining chain creates actual workshop aluminum.
        applied(w,ship,19,Action::Mine,silicate,30);
        applied(w,ship,20,Action::RefineMineral,10,12000);
        applied(w,ship,21,Action::TransferWorkshopFeed,site,1000);
        CHECK(w.world().total_material_kg()==initial_mass);
        auto rows=workforce02::plan_original_workshop(w.world(),7,ship,site);
        first_id=rows[0].suggested_npc;
        CHECK(first_id>0);
        auto proposed=workforce02::approved_original_assignment(
            w.world(),7,ship,site,false,first_id,22,true);
        CHECK(proposed.has_value());
        CHECK(proposed->action==Action::AssignWorkshopOperator);
        CHECK(proposed->issuer==7 && proposed->actor==ship &&
              proposed->target==site && proposed->amount==static_cast<std::int64_t>(first_id));
        CHECK(w.submit(*proposed));w.step();
        CHECK(w.world().events().back().result==Outcome::Applied);
        CHECK(w.world().workshop(site)->operator_npc_id==first_id);
        CHECK(w.world().workshop(site)->reserved_aluminum_kg==20);
        CHECK(w.world().total_material_kg()==initial_mass);
        rows=workforce02::plan_original_workshop(w.world(),7,ship,site);
        CHECK(rows[0].gate==workforce02::StaffGate::AlreadyStaffed);
        CHECK(!workforce02::approved_original_assignment(
            w.world(),7,ship,site,false,first_id,23,true).has_value());

        applied(w,ship,24,Action::ConstructWorkshopFinisher,site);
        rows=workforce02::plan_original_workshop(w.world(),7,ship,site);
        CHECK(rows[1].can_offer_assignment());
        second_id=rows[1].suggested_npc;
        CHECK(second_id>0 && second_id!=first_id);
        // Do not allow direct malicious duplicate named work, including
        // bypasses of the new *read-only* planner through existing commands.
        CHECK(send(w,ship,25,Action::AssignWorkshopFinisher,site,
                   static_cast<std::int64_t>(first_id)).result==Outcome::InvalidTarget);
        CHECK(w.world().workshop(site)->finisher_npc_id==0);
        proposed=workforce02::approved_original_assignment(
            w.world(),7,ship,site,true,second_id,26,true);
        CHECK(proposed.has_value() && proposed->action==Action::AssignWorkshopFinisher);
        CHECK(w.submit(*proposed));w.step();
        CHECK(w.world().events().back().result==Outcome::Applied);
        CHECK(w.world().workshop(site)->finisher_npc_id==second_id);
        CHECK(w.world().total_material_kg()==initial_mass);
        w.steps(16); // existing bounded physical work, not a second scheduler
        CHECK(w.world().workshop(site)->completed_batches>=2);
        CHECK(w.world().workshop(site)->finisher_batches>=1);
        CHECK(w.world().total_material_kg()==initial_mass);
        PersistentWorld::recover_copy(db,backup);
        {
            PersistentWorld recovered(backup,Simulation(98765));
            CHECK(recovered.world().workshop(site)->operator_npc_id==first_id);
            CHECK(recovered.world().workshop(site)->finisher_npc_id==second_id);
            CHECK(recovered.world().total_material_kg()==initial_mass);
            auto after=workforce02::plan_original_workshop(recovered.world(),7,ship,site);
            CHECK(after[0].gate==workforce02::StaffGate::AlreadyStaffed);
            CHECK(after[1].gate==workforce02::StaffGate::AlreadyStaffed);
            CHECK(after[0].suggested_npc==0 && after[1].suggested_npc==0);
        }
    }
    {
        PersistentWorld restarted(db,Simulation(42));
        CHECK(restarted.world().workshop(site)->operator_npc_id==first_id);
        CHECK(restarted.world().workshop(site)->finisher_npc_id==second_id);
        CHECK(restarted.world().total_material_kg()==initial_mass);
    }
    fs::remove_all(folder);
    std::cout<<"WO2: full professions, authorization, exclusive native staff, bounded work and SQLite recovery: PASS\n";
    return 0;
 }catch(const std::exception& e){
    std::cerr<<"WO2 native failure: "<<e.what()<<'\n';return 1;
 }
}
