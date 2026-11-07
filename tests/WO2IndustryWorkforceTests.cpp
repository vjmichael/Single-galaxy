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
        // WO2-G: a projected shift is bounded, does not mutate original
        // resources or reserve an NPC, and never projects a nonexistent shop.
        const auto before_hash=w.world().state_hash();
        const auto start=w.world().tick()+1;
        const auto preview=workforce02::plan_provisional_workshop_coverage(
            w.world(),7,ship,site,start,start+8);
        CHECK(preview.size()==2);
        CHECK(preview[0].gate==workforce02::CoverageGate::Provisional);
        CHECK(preview[0].worker==before[0].suggested_npc);
        CHECK(!preview[0].incumbent && !preview[0].reserved &&
              preview[0].requires_owner_command);
        CHECK(preview[1].gate==workforce02::CoverageGate::SiteUnavailable);
        CHECK(preview[1].worker==0);
        CHECK(workforce02::plan_provisional_workshop_coverage(
            w.world(),8,ship,site,start,start+8)[0].gate==
                workforce02::CoverageGate::SiteUnavailable);
        CHECK(workforce02::plan_provisional_workshop_coverage(
            w.world(),7,ship,site,w.world().tick(),start+8)[0].gate==
                workforce02::CoverageGate::InvalidWindow);
        CHECK(workforce02::plan_provisional_workshop_coverage(
            w.world(),7,ship,site,start,start+10001)[0].gate==
                workforce02::CoverageGate::InvalidWindow);
        CHECK(w.world().state_hash()==before_hash);
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
        // Native negative execution gates, not merely a UI preview:
        // the finisher does not exist yet and cannot award shape credits.
        const auto denied_shape=send(w,ship,200,Action::TrainWorkshopTrade,
                                      site,static_cast<std::int64_t>(first_id),2);
        CHECK(denied_shape.result==Outcome::InvalidTarget);
        CHECK(denied_shape.transferred==0);
        CHECK(w.world().workshop_training_units(first_id,WorkshopTrade::Shape)==0);
        CHECK(w.world().total_material_kg()==initial_mass);
        const auto denied_person=send(w,ship,201,Action::TrainWorkshopTrade,
                                       site,999999,1);
        CHECK(denied_person.result==Outcome::InvalidTarget);
        CHECK(denied_person.transferred==0);
        CHECK(w.world().workshop_training_units(999999,WorkshopTrade::Cut)==0);
        CHECK(w.world().total_material_kg()==initial_mass);
        const auto original_training_mass=w.world().total_material_kg();
        // WO2-H: training needs actual owner confirmation, a named resident,
        // a built powered machine, available finite stock and three separate
        // committed original-world receipts. No invented credentials on load.
        CHECK(!workforce02::approved_original_trade_training(
            w.world(),7,ship,site,first_id,WorkshopTrade::Cut,270,false));
        CHECK(!workforce02::approved_original_trade_training(
            w.world(),8,ship,site,first_id,WorkshopTrade::Cut,271,true));
        CHECK(!w.world().workshop_trade_certified(first_id,WorkshopTrade::Cut));
        for(std::uint64_t training_id=270;training_id<273;++training_id){
            auto training=workforce02::approved_original_trade_training(
                w.world(),7,ship,site,first_id,WorkshopTrade::Cut,
                training_id,true);
            CHECK(training.has_value());
            CHECK(training->action==Action::TrainWorkshopTrade);
            CHECK(training->issuer==7 && training->amount==static_cast<std::int64_t>(first_id) &&
                  training->thrust_x==1);
            const auto before_power=w.world().utility(site)->consumed_energy;
            const auto before_scrap=w.world().workshop(site)->scrap_kg;
            CHECK(w.submit(*training));
            // An approved order ID becomes consumed at admission, even before
            // the world tick applies it. An obsolete proposal cannot be reused.
            CHECK(!workforce02::approved_original_trade_training(
                w.world(),7,ship,site,first_id,WorkshopTrade::Cut,
                training_id,true));
            if(training_id==270){
                const auto pending_copy=(folder/"training_pending.sqlite").string();
                PersistentWorld::recover_copy(db,pending_copy);
                PersistentWorld queued(pending_copy,Simulation(491));
                CHECK(queued.world().workshop_training_units(first_id,WorkshopTrade::Cut)==0);
                CHECK(queued.world().has_command_id(training_id));
                queued.step();
                CHECK(queued.world().workshop_training_units(first_id,WorkshopTrade::Cut)==1);
                CHECK(queued.world().total_material_kg()==initial_mass);
                // Same committed tick: original command ordering executes
                // practice first, then rejects occupation of the same worker.
                Command double_book{};
                double_book.id=290;double_book.sequence=290;
                double_book.execute_tick=w.world().tick()+1;
                double_book.issuer=7;double_book.actor=ship;double_book.target=site;
                double_book.action=Action::AssignWorkshopOperator;
                double_book.amount=static_cast<std::int64_t>(first_id);
                // A second independent training command in this same tick
                // cannot create two units of practice/credit for one person.
                Command duplicate_practice=*training;
                duplicate_practice.id=289;duplicate_practice.sequence=289;
                CHECK(w.submit(duplicate_practice));
                CHECK(w.submit(double_book));
            }
            w.step();
            const auto& outcomes=w.world().events();
            if(training_id==270){
                CHECK(outcomes.size()>=3);
                CHECK(outcomes[outcomes.size()-3].command_id==training_id &&
                      outcomes[outcomes.size()-3].result==Outcome::Applied &&
                      outcomes[outcomes.size()-3].transferred==1);
                CHECK(outcomes[outcomes.size()-2].command_id==289 &&
                      outcomes[outcomes.size()-2].result==Outcome::InvalidTarget &&
                      outcomes[outcomes.size()-2].transferred==0);
                CHECK(outcomes.back().command_id==290 &&
                      outcomes.back().result==Outcome::InvalidTarget &&
                      outcomes.back().transferred==0);
                CHECK(w.world().workshop(site)->operator_npc_id==0);
            }else{
                CHECK(outcomes.back().command_id==training_id &&
                      outcomes.back().result==Outcome::Applied &&
                      outcomes.back().transferred==1);
            }
            CHECK(w.world().utility(site)->consumed_energy>=before_power+1);
            CHECK(w.world().workshop(site)->scrap_kg>=before_scrap+1);
            CHECK(w.world().total_material_kg()==original_training_mass);
            CHECK(w.world().workshop_training_units(first_id,WorkshopTrade::Cut)==training_id-269);
        }
        CHECK(w.world().workshop_trade_certified(first_id,WorkshopTrade::Cut));
        CHECK(!w.world().workshop_trade_certified(first_id,WorkshopTrade::Shape));
        CHECK(!workforce02::approved_original_trade_training(
            w.world(),7,ship,site,first_id,WorkshopTrade::Cut,273,true));
        CHECK(send(w,ship,273,Action::TrainWorkshopTrade,site,
                   static_cast<std::int64_t>(first_id),1).result==Outcome::InvalidArgument);
        CHECK(w.world().workshop_training_units(first_id,WorkshopTrade::Cut)==3);
        CHECK(w.world().total_material_kg()==original_training_mass);
        // Reject direct foreign-owner submissions before they reach a tick.
        Command foreign{};
        foreign.id=274;foreign.sequence=274;foreign.execute_tick=w.world().tick()+1;
        foreign.issuer=8;foreign.actor=ship;foreign.target=site;
        foreign.action=Action::TrainWorkshopTrade;foreign.amount=static_cast<std::int64_t>(first_id);
        foreign.thrust_x=1;
        CHECK(!w.submit(foreign));
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
        // Operator must produce REAL panels before shape practice is allowed.
        w.steps(8);
        CHECK(w.world().workshop(site)->panels_kg>=3);
        for(std::uint64_t training_id=280;training_id<283;++training_id){
            auto training=workforce02::approved_original_trade_training(
                w.world(),7,ship,site,second_id,WorkshopTrade::Shape,
                training_id,true);
            CHECK(training.has_value());
            const auto before_power=w.world().utility(site)->consumed_energy;
            const auto before_scrap=w.world().workshop(site)->scrap_kg;
            CHECK(w.submit(*training));w.step();
            CHECK(w.world().utility(site)->consumed_energy>=before_power+1);
            CHECK(w.world().workshop(site)->scrap_kg>=before_scrap+1);
            CHECK(w.world().events().back().command_id==training_id &&
                  w.world().events().back().result==Outcome::Applied);
            CHECK(w.world().total_material_kg()==initial_mass);
        }
        CHECK(w.world().workshop_trade_certified(second_id,WorkshopTrade::Shape));
        CHECK(!w.world().workshop_trade_certified(second_id,WorkshopTrade::Cut));
        // The actual original workshop staffing view reads the same durable
        // qualification without inventing an employment or training roster.
        const auto post_training=workforce02::plan_original_workshop(
            w.world(),7,ship,site);
        CHECK(post_training[1].gate==workforce02::StaffGate::Ready);
        CHECK(post_training[1].suggested_npc==second_id);
        CHECK(workforce02::inspect_trade_qualification(
            w.world(),second_id,WorkshopTrade::Shape).certified);
        CHECK(w.world().npc_records().at(first_id).industrial_training.size()==3);
        // No duplicate or foreign training receipt can be used to fabricate
        // a credential when restoring the independently validated world.
        const auto valid_snapshot=w.world().canonical_state();
        CHECK(Simulation::from_snapshot(valid_snapshot).workshop_trade_certified(
            second_id,WorkshopTrade::Shape));
        auto corrupted=valid_snapshot;
        const auto marker=corrupted.find("npctraining|");
        CHECK(marker!=std::string::npos);
        const auto finish=corrupted.find('\n',marker);
        CHECK(finish!=std::string::npos);
        corrupted.insert(finish+1,corrupted.substr(marker,finish-marker+1));
        bool rejected_duplicate=false;
        try{(void)Simulation::from_snapshot(corrupted);}
        catch(const std::exception&){rejected_duplicate=true;}
        CHECK(rejected_duplicate);
        // Original receipt order is historical, not an interchangeable set.
        // Reversing two legitimate credits for one NPC must fail closed.
        auto reversed=valid_snapshot;
        const auto credit1=reversed.find("npctraining|");
        CHECK(credit1!=std::string::npos);
        const auto credit1_end=reversed.find('\n',credit1);
        CHECK(credit1_end!=std::string::npos);
        const auto credit2=credit1_end+1;
        CHECK(reversed.compare(credit2,12,"npctraining|")==0);
        const auto credit2_end=reversed.find('\n',credit2);
        CHECK(credit2_end!=std::string::npos);
        const auto record1=reversed.substr(credit1,credit1_end-credit1+1);
        const auto record2=reversed.substr(credit2,credit2_end-credit2+1);
        reversed.replace(credit1,credit2_end-credit1+1,record2+record1);
        bool rejected_reordered=false;
        try{(void)Simulation::from_snapshot(reversed);}
        catch(const std::exception&){rejected_reordered=true;}
        CHECK(rejected_reordered);
        auto fabricated=valid_snapshot;
        const auto first_line=fabricated.find("npctraining|");
        const auto second_separator=fabricated.find('|',first_line+12);
        CHECK(second_separator!=std::string::npos);
        const auto third_separator=fabricated.find('|',second_separator+1);
        CHECK(third_separator!=std::string::npos);
        fabricated.replace(second_separator+1,
                           third_separator-second_separator-1,"9999999");
        bool rejected_forgery=false;
        try{(void)Simulation::from_snapshot(fabricated);}
        catch(const std::exception&){rejected_forgery=true;}
        CHECK(rejected_forgery);
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
        // An active operator cannot simultaneously be a shape trainee, even
        // though the frame finisher now exists and has physical input.
        const auto occupied_credit_count=
            w.world().workshop_training_units(first_id,WorkshopTrade::Shape);
        const auto occupied_train=send(w,ship,350,Action::TrainWorkshopTrade,
            site,static_cast<std::int64_t>(first_id),2);
        CHECK(occupied_train.result==Outcome::InvalidTarget &&
              occupied_train.transferred==0);
        CHECK(w.world().workshop_training_units(first_id,WorkshopTrade::Shape)==
              occupied_credit_count);
        CHECK(w.world().total_material_kg()==initial_mass);
        const auto coverage=workforce02::plan_provisional_workshop_coverage(
            w.world(),7,ship,site,w.world().tick()+1,w.world().tick()+6);
        CHECK(coverage.size()==2);
        CHECK(coverage[0].gate==workforce02::CoverageGate::Provisional &&
              coverage[1].gate==workforce02::CoverageGate::Provisional);
        CHECK(coverage[0].incumbent && coverage[1].incumbent);
        CHECK(coverage[0].worker==first_id && coverage[1].worker==second_id);
        CHECK(coverage[0].worker!=coverage[1].worker);
        CHECK(!coverage[0].reserved && !coverage[1].reserved);
        w.steps(16); // existing bounded physical work, not a second scheduler
        CHECK(w.world().workshop(site)->completed_batches>=2);
        CHECK(w.world().workshop(site)->finisher_batches>=1);
        CHECK(w.world().total_material_kg()==initial_mass);
        PersistentWorld::recover_copy(db,backup);
        {
            PersistentWorld recovered(backup,Simulation(98765));
            CHECK(recovered.world().workshop(site)->operator_npc_id==first_id);
            CHECK(recovered.world().workshop(site)->finisher_npc_id==second_id);
            CHECK(recovered.world().workshop_trade_certified(first_id,WorkshopTrade::Cut));
            CHECK(recovered.world().workshop_trade_certified(second_id,WorkshopTrade::Shape));
            CHECK(recovered.world().npc_records().at(second_id).industrial_training.size()==3);
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
        CHECK(restarted.world().workshop_trade_certified(first_id,WorkshopTrade::Cut));
        CHECK(restarted.world().workshop_trade_certified(second_id,WorkshopTrade::Shape));
        CHECK(restarted.world().total_material_kg()==initial_mass);
    }
    // WO2-I: one owner-authorized, bounded shift on the SAME original
    // workshop. Future accepted commands survive SQLite restart but do not
    // employ anyone until the actual execute tick.
    {
        PersistentWorld shifts(db,Simulation(40001));
        applied(shifts,ship,400,Action::AssignWorkshopOperator,site,0);
        applied(shifts,ship,401,Action::AssignWorkshopFinisher,site,0);
        CHECK(shifts.world().workshop(site)->operator_npc_id==0);
        CHECK(shifts.world().workshop(site)->finisher_npc_id==0);
        const auto start=shifts.world().tick()+3;
        const auto finish=start+4;
        CHECK(!workforce02::approved_original_workshop_shift(
            shifts.world(),7,ship,site,false,first_id,start,finish,410,false));
        CHECK(!workforce02::approved_original_workshop_shift(
            shifts.world(),8,ship,site,false,first_id,start,finish,410,true));
        CHECK(!workforce02::approved_original_workshop_shift(
            shifts.world(),7,ship,site,false,999999,start,finish,410,true));
        CHECK(!workforce02::approved_original_workshop_shift(
            shifts.world(),7,ship,site,false,first_id,start,start+10001,410,true));
        auto proposal=workforce02::approved_original_workshop_shift(
            shifts.world(),7,ship,site,false,first_id,start,finish,410,true);
        CHECK(proposal.has_value());
        CHECK(proposal->action==Action::StartWorkshopShift &&
              proposal->execute_tick==start && proposal->thrust_y==4);
        CHECK(shifts.submit(*proposal));
        // Original accepted future commitments cannot double-book a named
        // worker or the same machine slot over overlapping windows.
        Command same_slot=*proposal;
        same_slot.id=413;same_slot.sequence=413;
        same_slot.amount=static_cast<std::int64_t>(second_id);
        CHECK(!shifts.submit(same_slot));
        Command same_worker=*proposal;
        same_worker.id=414;same_worker.sequence=414;
        same_worker.thrust_x=2;
        CHECK(!shifts.submit(same_worker));
        CHECK(!workforce02::approved_original_workshop_shift(
            shifts.world(),7,ship,site,false,first_id,start,finish,410,true));
        const auto pending=(folder/"wo2i_pending.sqlite").string();
        PersistentWorld::recover_copy(db,pending);
        {
            PersistentWorld reopened(pending,Simulation(9999));
            CHECK(reopened.world().workshop(site)->operator_npc_id==0);
            CHECK(reopened.world().has_command_id(410));
            reopened.steps(3);
            CHECK(reopened.world().workshop(site)->operator_npc_id==first_id);
            CHECK(reopened.world().workshop(site)->shifts[0].command_id==410);
            CHECK(reopened.world().workshop(site)->shifts[0].end_tick==finish);
            CHECK(reopened.world().total_material_kg()==initial_mass);
        }
        shifts.steps(2);
        CHECK(shifts.world().workshop(site)->operator_npc_id==0);
        shifts.step();
        CHECK(shifts.world().workshop(site)->operator_npc_id==first_id);
        CHECK(shifts.world().workshop(site)->shifts[0].command_id==410);
        CHECK(shifts.world().total_material_kg()==initial_mass);
        const auto shift_snapshot=shifts.world().canonical_state();
        CHECK(Simulation::from_snapshot(shift_snapshot).workshop(site)->
              shifts[0].command_id==410);
        auto forged=shift_snapshot;
        const auto marker=forged.find("workshopshift|");
        CHECK(marker!=std::string::npos);
        const auto line_end=forged.find('\n',marker);
        CHECK(line_end!=std::string::npos);
        // One record cannot be duplicated to claim two original shifts.
        forged.insert(line_end+1,forged.substr(marker,line_end-marker+1));
        bool refused=false;
        try{(void)Simulation::from_snapshot(forged);}
        catch(const std::exception&){refused=true;}
        CHECK(refused);
        CHECK(!workforce02::approved_original_workshop_shift_cancel(
            shifts.world(),8,ship,site,false,410,411,true));
        CHECK(!workforce02::approved_original_workshop_shift_cancel(
            shifts.world(),7,ship,site,false,410,411,false));
        auto cancel=workforce02::approved_original_workshop_shift_cancel(
            shifts.world(),7,ship,site,false,410,411,true);
        CHECK(cancel.has_value());
        CHECK(shifts.submit(*cancel));
        shifts.step();
        CHECK(shifts.world().events().back().command_id==411 &&
              shifts.world().events().back().result==Outcome::Applied);
        CHECK(shifts.world().workshop(site)->operator_npc_id==0);
        CHECK(!shifts.world().workshop(site)->shifts[0].command_id);
        CHECK(send(shifts,ship,412,Action::CancelWorkshopShift,site,410,1).
              result==Outcome::InvalidTarget);
        // Relief is a real new owner-approved shift; expiry releases only the
        // actual worker slot without fabricating labor, input or output.
        const auto relief_start=shifts.world().tick()+1;
        auto relief=workforce02::approved_original_workshop_shift(
            shifts.world(),7,ship,site,true,second_id,relief_start,
            relief_start+2,420,true);
        CHECK(relief.has_value());
        CHECK(shifts.submit(*relief));
        shifts.step();
        CHECK(shifts.world().workshop(site)->finisher_npc_id==second_id);
        CHECK(shifts.world().workshop(site)->shifts[1].command_id==420);
        shifts.steps(2);
        CHECK(shifts.world().workshop(site)->finisher_npc_id==0);
        CHECK(!shifts.world().workshop(site)->shifts[1].command_id);
        CHECK(shifts.world().total_material_kg()==initial_mass);
        // A future authenticated order can be cancelled before its due tick
        // without deleting the original accepted order or creating labor.
        const auto later=shifts.world().tick()+5;
        auto future=workforce02::approved_original_workshop_shift(
            shifts.world(),7,ship,site,false,first_id,later,later+3,430,true);
        CHECK(future.has_value() && shifts.submit(*future));
        CHECK(shifts.world().pending_workshop_shift(site,false,430));
        CHECK(!workforce02::approved_original_workshop_shift_cancel(
            shifts.world(),8,ship,site,false,430,431,true));
        auto stop_future=workforce02::approved_original_workshop_shift_cancel(
            shifts.world(),7,ship,site,false,430,431,true);
        CHECK(stop_future.has_value() && shifts.submit(*stop_future));
        shifts.step();
        CHECK(shifts.world().events().back().command_id==431 &&
              shifts.world().events().back().result==Outcome::Applied);
        CHECK(shifts.world().workshop(site)->cancelled_pending_shifts.count(430));
        CHECK(!shifts.world().pending_workshop_shift(site,false,430));
        const auto cancelled_copy=(folder/"wo2i_cancelled_pending.sqlite").string();
        PersistentWorld::recover_copy(db,cancelled_copy);
        {
            PersistentWorld copied(cancelled_copy,Simulation(54321));
            CHECK(copied.world().workshop(site)->cancelled_pending_shifts.count(430));
            CHECK(copied.world().total_material_kg()==initial_mass);
        }
        while(shifts.world().tick()<later)shifts.step();
        CHECK(shifts.world().events().back().command_id==430 &&
              shifts.world().events().back().result==Outcome::InvalidTarget);
        CHECK(shifts.world().workshop(site)->operator_npc_id==0);
        CHECK(shifts.world().workshop(site)->cancelled_pending_shifts.empty());
        CHECK(shifts.world().total_material_kg()==initial_mass);
        const auto finished_copy=(folder/"wo2i_recovered.sqlite").string();
        PersistentWorld::recover_copy(db,finished_copy);
        PersistentWorld recovered(finished_copy,Simulation(555));
        CHECK(recovered.world().workshop(site)->operator_npc_id==0);
        CHECK(recovered.world().workshop(site)->finisher_npc_id==0);
        CHECK(recovered.world().total_material_kg()==initial_mass);
    }
    // Independently read the SAME original SQLite resource journal. Six
    // successful per-NPC practice commands must produce exactly six
    // mass-conserving inventory-to-scrap transfers, not shadow skill XP.
    sqlite3* audit=nullptr;
    CHECK(sqlite3_open_v2(db.c_str(),&audit,SQLITE_OPEN_READONLY,nullptr)==SQLITE_OK);
    sqlite3_stmt* rows=nullptr;
    CHECK(sqlite3_prepare_v2(audit,
        "SELECT amount,source_id,destination_id FROM resource_ledger "
        "WHERE reason='industrial_training_practice_kg' ORDER BY id",
        -1,&rows,nullptr)==SQLITE_OK);
    std::size_t cut_receipts=0,shape_receipts=0;
    int rc=SQLITE_ROW;
    while((rc=sqlite3_step(rows))==SQLITE_ROW){
        CHECK(sqlite3_column_int64(rows,0)==1);
        const auto source=std::string(reinterpret_cast<const char*>(
            sqlite3_column_text(rows,1)));
        const auto target=std::string(reinterpret_cast<const char*>(
            sqlite3_column_text(rows,2)));
        CHECK(target=="workshop:"+std::to_string(site)+":aluminum_scrap");
        if(source=="workshop:"+std::to_string(site)+":aluminum")
            ++cut_receipts;
        else if(source=="workshop:"+std::to_string(site)+":aluminum_panels")
            ++shape_receipts;
        else CHECK(false);
    }
    CHECK(rc==SQLITE_DONE);
    CHECK(sqlite3_finalize(rows)==SQLITE_OK);
    CHECK(sqlite3_close(audit)==SQLITE_OK);
    CHECK(cut_receipts==3 && shape_receipts==3);
    // Adversarial independent SQLite copy: altering the physical practice
    // receipt must either be denied by SQLite or rejected on original-world
    // recovery. A snapshot-only skill count is never sufficient evidence.
    const auto tampered=(folder/"training_ledger_tamper.sqlite").string();
    PersistentWorld::recover_copy(db,tampered);
    sqlite3* adversary=nullptr;
    CHECK(sqlite3_open_v2(tampered.c_str(),&adversary,
                          SQLITE_OPEN_READWRITE,nullptr)==SQLITE_OK);
    char* sql_error=nullptr;
    const int mutated=sqlite3_exec(adversary,
        "UPDATE resource_ledger SET amount=2 WHERE id=("
        "SELECT id FROM resource_ledger "
        "WHERE reason='industrial_training_practice_kg' ORDER BY id LIMIT 1)",
        nullptr,nullptr,&sql_error);
    if(sql_error)sqlite3_free(sql_error);
    if(mutated==SQLITE_OK)CHECK(sqlite3_changes(adversary)==1);
    CHECK(sqlite3_close(adversary)==SQLITE_OK);
    if(mutated==SQLITE_OK){
        bool fail_closed=false;
        try{PersistentWorld forged(tampered,Simulation(5678));}
        catch(const std::exception&){fail_closed=true;}
        CHECK(fail_closed);
    }
    fs::remove_all(folder);
    std::cout<<"WO2: full professions, authorization, exclusive native staff, bounded work and SQLite recovery: PASS\n";
    return 0;
 }catch(const std::exception& e){
    std::cerr<<"WO2 native failure: "<<e.what()<<'\n';return 1;
 }
}
