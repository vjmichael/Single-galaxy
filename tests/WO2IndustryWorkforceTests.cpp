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
            if(training_id==270){
                // The very first WO2 v105 command on a legacy original ship
                // must prepare honest v71 frame/physical-system custody BEFORE
                // its pending SQLite journal can be independently reopened.
                const auto* actual=w.world().find(ship);
                CHECK(actual!=nullptr);
                const auto frame=w.world().ship_frames().find(ship);
                CHECK(frame!=w.world().ship_frames().end());
                CHECK(frame->second.system==actual->system_id &&
                      frame->second.anchor_body==0);
                CHECK(w.world().physical_celestial_systems().count(actual->system_id)==1);
            }
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
        const auto staffed=workforce02::plan_original_workshop(
            shifts.world(),7,ship,site);
        CHECK(staffed[0].shift_command_id==410 &&
              staffed[0].shift_end_tick==finish);
        const auto beyond=workforce02::plan_provisional_workshop_coverage(
            shifts.world(),7,ship,site,shifts.world().tick()+1,finish+1);
        CHECK(beyond[0].gate==workforce02::CoverageGate::ShiftWindowUncovered);
        CHECK(!beyond[0].reserved);
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
        // WO2-I atomic handover: one original owner command replaces only
        // the named worker; it keeps the same shift ID, end tick and WIP.
        CHECK(!workforce02::approved_original_workshop_shift_relief(
            shifts.world(),7,ship,site,false,410,second_id,419,false));
        CHECK(!workforce02::approved_original_workshop_shift_relief(
            shifts.world(),8,ship,site,false,410,second_id,419,true));
        CHECK(!workforce02::approved_original_workshop_shift_relief(
            shifts.world(),7,ship,site,false,999999,second_id,419,true));
        auto handover=workforce02::approved_original_workshop_shift_relief(
            shifts.world(),7,ship,site,false,410,second_id,419,true);
        CHECK(handover.has_value() &&
              handover->action==Action::RelieveWorkshopShift &&
              handover->amount==410 &&
              handover->thrust_y==static_cast<int>(second_id));
        const auto original_end=shifts.world().workshop(site)->shifts[0].end_tick;
        CHECK(shifts.submit(*handover));
        Command overlapping_handover=*handover;
        overlapping_handover.id=417;overlapping_handover.sequence=417;
        CHECK(!shifts.submit(overlapping_handover));
        shifts.step();
        CHECK(shifts.world().events().back().command_id==419 &&
              shifts.world().events().back().result==Outcome::Applied);
        CHECK(shifts.world().workshop(site)->operator_npc_id==second_id);
        CHECK(shifts.world().workshop(site)->shifts[0].command_id==410);
        CHECK(shifts.world().workshop(site)->shifts[0].relief_command_id==419);
        CHECK(shifts.world().workshop(site)->shifts[0].end_tick==original_end);
        const auto relieved_slot=workforce02::plan_original_workshop(
            shifts.world(),7,ship,site);
        CHECK(relieved_slot[0].assigned_npc==second_id &&
              relieved_slot[0].shift_command_id==410 &&
              relieved_slot[0].shift_end_tick==finish);
        // Native production advances this same tick; preserved total mass and
        // the unchanged original end tick are the relevant invariants.
        CHECK(shifts.world().total_material_kg()==initial_mass);
        // Replay the same original SQLite journal after handover and check
        // that it recovers the *new* worker, not the original start worker.
        const auto relief_copy=(folder/"wo2i_atomic_relief.sqlite").string();
        PersistentWorld::recover_copy(db,relief_copy);
        {
            PersistentWorld reopened(relief_copy,Simulation(760));
            CHECK(reopened.world().workshop(site)->operator_npc_id==second_id);
            CHECK(reopened.world().workshop(site)->shifts[0].command_id==410);
            CHECK(reopened.world().workshop(site)->shifts[0].relief_command_id==419);
            CHECK(reopened.world().workshop(site)->shifts[0].end_tick==finish);
            CHECK(reopened.world().total_material_kg()==initial_mass);
        }
        // A second independently confirmed handover at the next tick may
        // use a *lower* command ID. Recovery orders by true applied tick.
        auto reverse=workforce02::approved_original_workshop_shift_relief(
            shifts.world(),7,ship,site,false,410,first_id,418,true);
        CHECK(reverse.has_value() && shifts.submit(*reverse));
        shifts.step();
        CHECK(shifts.world().events().back().command_id==418 &&
              shifts.world().events().back().result==Outcome::Applied);
        CHECK(shifts.world().workshop(site)->operator_npc_id==first_id);
        CHECK(shifts.world().workshop(site)->shifts[0].relief_command_id==418);
        CHECK(shifts.world().workshop(site)->shifts[0].end_tick==finish);
        CHECK(shifts.world().total_material_kg()==initial_mass);
        const auto second_relief=(folder/"wo2i_two_reliefs.sqlite").string();
        PersistentWorld::recover_copy(db,second_relief);
        {
            PersistentWorld recovered(second_relief,Simulation(761));
            CHECK(recovered.world().workshop(site)->operator_npc_id==first_id);
            CHECK(recovered.world().workshop(site)->shifts[0].relief_command_id==418);
        }
        auto altered_relief=shifts.world().canonical_state();
        const auto shift_row=altered_relief.find("workshopshift|");
        CHECK(shift_row!=std::string::npos);
        const auto shift_row_end=altered_relief.find('\n',shift_row);
        CHECK(shift_row_end!=std::string::npos);
        const auto final_pipe=altered_relief.rfind('|',shift_row_end);
        CHECK(final_pipe!=std::string::npos && final_pipe>shift_row);
        altered_relief.replace(final_pipe+1,shift_row_end-final_pipe-1,"999999");
        bool rejected_relief_forgery=false;
        try{(void)Simulation::from_snapshot(altered_relief);}
        catch(const std::exception&){rejected_relief_forgery=true;}
        CHECK(rejected_relief_forgery);
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
        // Two separately owner-approved, nonoverlapping future shifts can
        // replace one another at the exact end-exclusive boundary.
        const auto first_start=shifts.world().tick()+1;
        const auto first_end=first_start+2;
        auto first_shift=workforce02::approved_original_workshop_shift(
            shifts.world(),7,ship,site,false,first_id,
            first_start,first_end,440,true);
        auto next_shift=workforce02::approved_original_workshop_shift(
            shifts.world(),7,ship,site,false,second_id,
            first_end,first_end+2,441,true);
        CHECK(first_shift.has_value() && next_shift.has_value());
        CHECK(shifts.submit(*first_shift));
        CHECK(shifts.submit(*next_shift));
        shifts.step();
        CHECK(shifts.world().workshop(site)->operator_npc_id==first_id);
        shifts.step();
        CHECK(shifts.world().workshop(site)->operator_npc_id==first_id);
        shifts.step(); // end of first, start of second, zero phantom overlap
        CHECK(shifts.world().events().back().command_id==441 &&
              shifts.world().events().back().result==Outcome::Applied);
        CHECK(shifts.world().workshop(site)->operator_npc_id==second_id);
        CHECK(shifts.world().workshop(site)->shifts[0].command_id==441);
        shifts.steps(2);
        CHECK(shifts.world().workshop(site)->operator_npc_id==0);
        CHECK(!shifts.world().workshop(site)->shifts[0].command_id);
        CHECK(shifts.world().total_material_kg()==initial_mass);
        const auto finished_copy=(folder/"wo2i_recovered.sqlite").string();
        PersistentWorld::recover_copy(db,finished_copy);
        PersistentWorld recovered(finished_copy,Simulation(555));
        CHECK(recovered.world().workshop(site)->operator_npc_id==0);
        CHECK(recovered.world().workshop(site)->finisher_npc_id==0);
        CHECK(recovered.world().total_material_kg()==initial_mass);
    }
    // WO2-J isolated original-world copy: do not change the WO2-H primary
    // SQLite ledger that must prove exactly six historical solo credits.
    {
        const auto management_db=(folder/"wo2j_supervisor.sqlite").string();
        PersistentWorld::recover_copy(db,management_db);
        PersistentWorld managed(management_db,Simulation(106));
        CHECK(managed.world().workshop(site)->operator_npc_id==0);
        CHECK(managed.world().workshop_trade_certified(first_id,WorkshopTrade::Cut));
        CHECK(managed.world().workshop_trade_certified(second_id,WorkshopTrade::Shape));
        const auto initial=managed.world().total_material_kg();
        CHECK(!workforce02::approved_workshop_supervisor(
            managed.world(),7,ship,site,WorkshopTrade::Cut,first_id,710,false));
        CHECK(!workforce02::approved_workshop_supervisor(
            managed.world(),8,ship,site,WorkshopTrade::Cut,first_id,710,true));
        CHECK(!workforce02::approved_workshop_supervisor(
            managed.world(),7,ship,site,WorkshopTrade::Cut,second_id,710,true));
        auto appoint=workforce02::approved_workshop_supervisor(
            managed.world(),7,ship,site,WorkshopTrade::Cut,first_id,710,true);
        CHECK(appoint.has_value() && managed.submit(*appoint));
        CHECK(!workforce02::approved_workshop_supervisor(
            managed.world(),7,ship,site,WorkshopTrade::Cut,first_id,710,true));
        managed.step();
        CHECK(managed.world().events().back().command_id==710);
        CHECK(managed.world().events().back().result==Outcome::Applied);
        CHECK(managed.world().workshop(site)->supervisor_npc_id==first_id);
        CHECK(managed.world().workshop(site)->supervisor_appointment_id==710);
        // No management appointment makes an automated NPC or resources.
        CHECK(managed.world().npc_records().count(first_id));
        CHECK(managed.world().total_material_kg()==initial);
        auto board=workforce02::workshop_job_board(
            managed.world(),7,ship,site);
        CHECK(board[0].supervisor==first_id && !board[0].approved);
        CHECK(!workforce02::approved_workshop_job(
            managed.world(),7,ship,site,WorkshopTrade::Cut,1,711,false));
        CHECK(!workforce02::approved_workshop_job(
            managed.world(),8,ship,site,WorkshopTrade::Cut,1,711,true));
        auto job=workforce02::approved_workshop_job(
            managed.world(),7,ship,site,WorkshopTrade::Cut,1,711,true);
        CHECK(job.has_value());
        // Read-only proposal checks are not authority. Host submission also
        // rejects a stolen owner's exact command and a forged station target.
        Command foreign_job=*job;
        foreign_job.id=721;foreign_job.sequence=721;foreign_job.issuer=8;
        CHECK(!managed.submit(foreign_job));
        Command cross_site_job=*job;
        cross_site_job.id=722;cross_site_job.sequence=722;
        cross_site_job.target=ship; // a ship is not the original installed workshop
        CHECK(!managed.submit(cross_site_job));
        CHECK(managed.submit(*job));
        managed.step();
        CHECK(managed.world().events().back().command_id==711);
        CHECK(managed.world().events().back().result==Outcome::Applied);
        board=workforce02::workshop_job_board(managed.world(),7,ship,site);
        CHECK(board[0].approval_command==711 &&
              board[0].owner_target==1 &&
              board[0].physically_completed==0);
        CHECK(!board[1].approved);
        // The mentor's name and valid original appointment are required.
        CHECK(!workforce02::approved_workshop_mentorship(
            managed.world(),8,ship,site,WorkshopTrade::Cut,second_id,712,true));
        CHECK(!workforce02::approved_workshop_mentorship(
            managed.world(),7,ship,site,WorkshopTrade::Cut,first_id,712,true));
        auto mentor=workforce02::approved_workshop_mentorship(
            managed.world(),7,ship,site,WorkshopTrade::Cut,second_id,712,true);
        CHECK(mentor.has_value());
        Command foreign_mentor=*mentor;
        foreign_mentor.id=723;foreign_mentor.sequence=723;foreign_mentor.issuer=8;
        CHECK(!managed.submit(foreign_mentor));
        CHECK(managed.submit(*mentor));
        Command duplicate_mentorship=*mentor;
        duplicate_mentorship.id=716;duplicate_mentorship.sequence=716;
        CHECK(managed.submit(duplicate_mentorship));
        const auto before_energy=managed.world().utility(site)->consumed_energy;
        const auto before_credits=managed.world().workshop_training_units(
            second_id,WorkshopTrade::Cut);
        managed.step();
        CHECK(managed.world().events().size()>=2);
        const auto& mentor_results=managed.world().events();
        CHECK(mentor_results[mentor_results.size()-2].command_id==712 &&
              mentor_results[mentor_results.size()-2].result==Outcome::Applied &&
              mentor_results[mentor_results.size()-2].transferred==1);
        CHECK(mentor_results.back().command_id==716 &&
              mentor_results.back().result==Outcome::InvalidTarget &&
              mentor_results.back().transferred==0);
        CHECK(managed.world().workshop_training_units(
            second_id,WorkshopTrade::Cut)==before_credits+1);
        const auto& latest=managed.world().npc_records().at(second_id).
            industrial_training.back();
        CHECK(latest.mentor_id==first_id &&
              latest.supervisor_appointment_id==710 &&
              latest.command_id==712 &&
              latest.trade==WorkshopTrade::Cut);
        CHECK(managed.world().utility(site)->consumed_energy>=before_energy+1);
        CHECK(managed.world().total_material_kg()==initial);
        // Only an actual original workshop batch can satisfy the owner
        // program. After one quota batch, native production must not exceed
        // the approved cap even when the physical operator keeps working.
        const auto base=managed.world().workshop(site)->
            approved_jobs[0].baseline_batches;
        CHECK(managed.world().workshop(site)->aluminum_kg>=20 ||
              managed.world().workshop(site)->reserved_aluminum_kg==20);
        CHECK(send(managed,ship,717,Action::AssignWorkshopOperator,site,
                   static_cast<std::int64_t>(second_id)).result==Outcome::Applied);
        // Reopen while physical work is underway, then advance the original
        // and recovered SQLite histories independently across the quota.
        managed.steps(2);
        const auto running_copy=(folder/"wo2j_running_recovered.sqlite").string();
        PersistentWorld::recover_copy(management_db,running_copy);
        managed.steps(13);
        const auto real_completed=managed.world().workshop(site)->completed_batches;
        // Zero completed batches is NOT acceptance of a working job board.
        CHECK(real_completed==base+1);
        board=workforce02::workshop_job_board(managed.world(),7,ship,site);
        CHECK(board[0].physically_completed==1 &&
              board[0].owner_target==1 && board[0].fulfilled &&
              !board[0].suspended);
        CHECK(workforce02::plan_original_workshop(
            managed.world(),7,ship,site)[0].work_state==
            workforce02::WorkState::ApprovedWorkComplete);
        CHECK(managed.world().total_material_kg()==initial);
        managed.steps(15); // original operator remains, but cannot exceed quota
        CHECK(managed.world().workshop(site)->completed_batches==real_completed);
        CHECK(managed.world().total_material_kg()==initial);
        {
            PersistentWorld running(running_copy,Simulation(1051));
            running.steps(28);
            CHECK(running.world().workshop(site)->completed_batches==real_completed);
            CHECK(running.world().workshop(site)->approved_jobs[0].command_id==711);
            CHECK(running.world().total_material_kg()==initial);
        }
        const auto manager_copy=(folder/"wo2j_active_recovered.sqlite").string();
        PersistentWorld::recover_copy(management_db,manager_copy);
        {
            PersistentWorld reopened(manager_copy,Simulation(107));
            CHECK(reopened.world().workshop(site)->supervisor_npc_id==first_id);
            CHECK(reopened.world().workshop(site)->approved_jobs[0].command_id==711);
            CHECK(reopened.world().npc_records().at(second_id).
                 industrial_training.back().supervisor_appointment_id==710);
            CHECK(reopened.world().total_material_kg()==initial);
        }
        // A physically reassigned supervisor must suspend, not erase or
        // silently unlimit, a fresh unfinished owner-approved job. Perform
        // this on its own SQLite copy so the main cancel/revoke history
        // below remains independently auditable.
        const auto suspension_db=(folder/"wo2j_supervisor_departure.sqlite").string();
        PersistentWorld::recover_copy(management_db,suspension_db);
        {
            PersistentWorld absent(suspension_db,Simulation(1052));
            CHECK(send(absent,ship,718,Action::CancelWorkshopJob,site,711,1).
                  result==Outcome::Applied);
            auto extended=workforce02::approved_workshop_job(
                absent.world(),7,ship,site,WorkshopTrade::Cut,4,719,true);
            CHECK(extended.has_value() && absent.submit(*extended));
            absent.step();
            CHECK(absent.world().events().back().result==Outcome::Applied);
            CHECK(absent.world().workshop(site)->approved_jobs[0].command_id==719);
            CHECK(first_id<second_id); // original resident order selects the mentor
            CHECK(send(absent,ship,720,Action::AssignHabitatResidents,site,-1).
                  result==Outcome::Applied);
            CHECK(absent.world().npc_records().at(first_id).location==ship);
            CHECK(absent.world().workshop(site)->supervisor_npc_id==0);
            const auto suspended=workforce02::workshop_job_board(
                absent.world(),7,ship,site);
            CHECK(suspended[0].approved && suspended[0].suspended &&
                  suspended[0].approval_command==719 &&
                  suspended[0].owner_target==4 && !suspended[0].fulfilled);
            const auto halted_batches=absent.world().workshop(site)->completed_batches;
            absent.steps(15);
            CHECK(absent.world().workshop(site)->completed_batches==halted_batches);
            CHECK(absent.world().total_material_kg()==initial);
            const auto departure_copy=(folder/"wo2j_suspended_recovered.sqlite").string();
            PersistentWorld::recover_copy(suspension_db,departure_copy);
            PersistentWorld suspended_replay(departure_copy,Simulation(1053));
            const auto restored_board=workforce02::workshop_job_board(
                suspended_replay.world(),7,ship,site);
            CHECK(restored_board[0].approved && restored_board[0].suspended &&
                  restored_board[0].approval_command==719);
            CHECK(suspended_replay.world().workshop(site)->completed_batches==
                  halted_batches);
            CHECK(suspended_replay.world().total_material_kg()==initial);
        }
        // A workshop supervisor must remain the SAME named NPC while the
        // original WO2-I machine shift performs one atomic worker handoff.
        // A new finite local resident supplies the replacement; no phantom
        // staffing pool or independently executed job is introduced.
        const auto handoff_db=(folder/"wo2j_managed_handoff.sqlite").string();
        PersistentWorld::recover_copy(management_db,handoff_db);
        {
            PersistentWorld combined(handoff_db,Simulation(1054));
            CHECK(send(combined,ship,730,Action::CancelWorkshopJob,site,711,1).
                  result==Outcome::Applied);
            CHECK(send(combined,ship,731,Action::AssignWorkshopOperator,site,0).
                  result==Outcome::Applied);
            CHECK(send(combined,ship,732,Action::AssignHabitatResidents,site,1).
                  result==Outcome::Applied);
            std::uint64_t replacement=0;
            for(const auto& [id,n]:combined.world().npc_records())
                if(id!=first_id && id!=second_id && n.home_ship==ship &&
                   n.location==site && n.assigned_site==site &&
                   n.role==NpcRole::Habitat)replacement=id;
            CHECK(replacement>0 && replacement!=first_id);
            auto program=workforce02::approved_workshop_job(
                combined.world(),7,ship,site,WorkshopTrade::Cut,2,733,true);
            CHECK(program.has_value() && combined.submit(*program));
            combined.step();
            CHECK(combined.world().events().back().result==Outcome::Applied);
            const auto begin=combined.world().tick()+2;
            auto duty=workforce02::approved_original_workshop_shift(
                combined.world(),7,ship,site,false,second_id,
                begin,begin+12,734,true);
            CHECK(duty.has_value() && combined.submit(*duty));
            combined.steps(2);
            CHECK(combined.world().workshop(site)->shifts[0].command_id==734 &&
                  combined.world().workshop(site)->operator_npc_id==second_id);
            auto handover=workforce02::approved_original_workshop_shift_relief(
                combined.world(),7,ship,site,false,734,replacement,735,true);
            CHECK(handover.has_value() && combined.submit(*handover));
            combined.step();
            CHECK(combined.world().events().back().result==Outcome::Applied);
            CHECK(combined.world().workshop(site)->operator_npc_id==replacement &&
                  combined.world().workshop(site)->shifts[0].worker==replacement &&
                  combined.world().workshop(site)->shifts[0].relief_command_id==735 &&
                  combined.world().workshop(site)->supervisor_npc_id==first_id &&
                  combined.world().workshop(site)->approved_jobs[0].command_id==733);
            CHECK(combined.world().total_material_kg()==initial);
            const auto handoff_copy=(folder/"wo2j_managed_handoff_recovered.sqlite").string();
            PersistentWorld::recover_copy(handoff_db,handoff_copy);
            {
                PersistentWorld reopened(handoff_copy,Simulation(1055));
                CHECK(reopened.world().workshop(site)->operator_npc_id==replacement);
                CHECK(reopened.world().workshop(site)->shifts[0].relief_command_id==735);
                CHECK(reopened.world().workshop(site)->supervisor_appointment_id==710);
                CHECK(reopened.world().workshop(site)->approved_jobs[0].command_id==733);
                CHECK(reopened.world().total_material_kg()==initial);
            }
            combined.steps(18);
            const auto board_after_handoff=workforce02::workshop_job_board(
                combined.world(),7,ship,site);
            CHECK(board_after_handoff[0].physically_completed<=2);
            CHECK(combined.world().workshop(site)->operator_npc_id==0);
            CHECK(combined.world().total_material_kg()==initial);
        }
        auto fake=managed.world().canonical_state();
        const auto mentor_marker=fake.find("npctraining|"+
            std::to_string(second_id)+"|712|");
        CHECK(mentor_marker!=std::string::npos);
        const auto mentor_line_end=fake.find('\n',mentor_marker);
        const auto mentor_last_pipe=fake.rfind('|',mentor_line_end);
        CHECK(mentor_last_pipe>mentor_marker);
        fake.replace(mentor_last_pipe+1,mentor_line_end-mentor_last_pipe-1,"999999");
        bool forged=false;
        try{(void)Simulation::from_snapshot(fake);}
        catch(const std::exception&){forged=true;}
        CHECK(forged);
        auto job_forged=managed.world().canonical_state();
        const auto job_marker=job_forged.find("workshopjob|"+
            std::to_string(site)+"|1|711|");
        CHECK(job_marker!=std::string::npos);
        job_forged.replace(job_marker,("workshopjob|"+
            std::to_string(site)+"|1|711|").size(),
            "workshopjob|"+std::to_string(site)+"|1|999999|");
        bool rejected_job=false;
        try{(void)Simulation::from_snapshot(job_forged);}
        catch(const std::exception&){rejected_job=true;}
        CHECK(rejected_job);
        // Cancel the approved job with the owner. This is not a material
        // refund or a permission for the supervisor to impersonate the owner.
        CHECK(!workforce02::approved_workshop_job_cancel(
            managed.world(),8,ship,site,WorkshopTrade::Cut,711,713,true));
        auto cancel=workforce02::approved_workshop_job_cancel(
            managed.world(),7,ship,site,WorkshopTrade::Cut,711,713,true);
        CHECK(cancel.has_value() && managed.submit(*cancel));
        managed.step();
        CHECK(managed.world().events().back().command_id==713 &&
              managed.world().events().back().result==Outcome::Applied);
        CHECK(!managed.world().workshop(site)->approved_jobs[0].command_id);
        // Management revocation does not delete original NPC training.
        auto revoke=workforce02::approved_workshop_supervisor(
            managed.world(),7,ship,site,WorkshopTrade::Cut,0,714,true);
        CHECK(revoke.has_value() && managed.submit(*revoke));
        managed.step();
        CHECK(managed.world().workshop(site)->supervisor_npc_id==0);
        CHECK(managed.world().workshop(site)->supervisor_appointment_id==0);
        CHECK(managed.world().workshop_training_units(
            second_id,WorkshopTrade::Cut)==before_credits+1);
        CHECK(managed.world().total_material_kg()==initial);
        const auto revoked_copy=(folder/"wo2j_revoked_recovered.sqlite").string();
        PersistentWorld::recover_copy(management_db,revoked_copy);
        PersistentWorld recovered(revoked_copy,Simulation(108));
        CHECK(!recovered.world().workshop(site)->supervisor_npc_id);
        CHECK(recovered.world().npc_records().at(second_id).
              industrial_training.back().mentor_id==first_id);
        CHECK(recovered.world().total_material_kg()==initial);
        // A future original owner-approved shift is a real commitment.
        // A manager proposal and a forged direct owner command must both
        // refuse to appoint that worker until the original shift is cancelled.
        const auto pending_db=(folder/"wo2j_future_shift_conflict.sqlite").string();
        PersistentWorld::recover_copy(management_db,pending_db);
        {
            PersistentWorld booked(pending_db,Simulation(1058));
            CHECK(send(booked,ship,750,Action::AssignWorkshopOperator,site,0).
                  result==Outcome::Applied);
            const auto due=booked.world().tick()+3;
            auto future=workforce02::approved_original_workshop_shift(
                booked.world(),7,ship,site,false,first_id,due,due+4,751,true);
            CHECK(future.has_value() && booked.submit(*future));
            CHECK(booked.world().pending_workshop_worker_duty(
                first_id,booked.world().tick()+1));
            CHECK(!workforce02::approved_workshop_supervisor(
                booked.world(),7,ship,site,WorkshopTrade::Cut,first_id,752,true));
            CHECK(send(booked,ship,752,Action::AppointWorkshopSupervisor,site,
                       static_cast<std::int64_t>(first_id),1).
                  result==Outcome::InvalidTarget);
            CHECK(booked.world().workshop(site)->supervisor_npc_id==0);
            auto cancel=workforce02::approved_original_workshop_shift_cancel(
                booked.world(),7,ship,site,false,751,753,true);
            CHECK(cancel.has_value() && booked.submit(*cancel));
            booked.step();
            CHECK(booked.world().events().back().command_id==753 &&
                  booked.world().events().back().result==Outcome::Applied);
            CHECK(!booked.world().pending_workshop_worker_duty(
                first_id,booked.world().tick()+1));
            booked.step(); // the original cancelled start still gets a refusal
            CHECK(booked.world().events().back().command_id==751 &&
                  booked.world().events().back().result==Outcome::InvalidTarget);
            auto appoint_after=workforce02::approved_workshop_supervisor(
                booked.world(),7,ship,site,WorkshopTrade::Cut,first_id,754,true);
            CHECK(appoint_after.has_value() && booked.submit(*appoint_after));
            booked.step();
            CHECK(booked.world().events().back().result==Outcome::Applied &&
                  booked.world().workshop(site)->supervisor_appointment_id==754);
            const auto pending_replay=(folder/"wo2j_future_shift_recovered.sqlite").string();
            PersistentWorld::recover_copy(pending_db,pending_replay);
            PersistentWorld reopened(pending_replay,Simulation(1059));
            CHECK(reopened.world().workshop(site)->supervisor_appointment_id==754);
            CHECK(reopened.world().workshop(site)->supervisor_npc_id==first_id);
            CHECK(reopened.world().total_material_kg()==initial);
        }
        // The SECOND installed original machine is equally real: appointment,
        // panel-consuming shape mentorship, one actual shaped-frame batch,
        // physical cap and SQLite replay must all use the same original NPCs.
        const auto shape_db=(folder/"wo2j_shape_supervisor.sqlite").string();
        PersistentWorld::recover_copy(management_db,shape_db);
        {
            PersistentWorld shaped(shape_db,Simulation(1056));
            CHECK(send(shaped,ship,740,Action::AssignWorkshopOperator,site,0).
                  result==Outcome::Applied);
            // The shape-chain test needs actual panel inventory, not an
            // assumed residual from another workshop test. Have the original
            // cut machine physically make panel feed with its named worker,
            // then release the worker before supervised shape practice.
            CHECK(send(shaped,ship,745,Action::AssignWorkshopOperator,site,
                       static_cast<std::int64_t>(first_id)).result==Outcome::Applied);
            shaped.steps(15);
            CHECK(shaped.world().workshop(site)->panels_kg>=21);
            CHECK(send(shaped,ship,746,Action::AssignWorkshopOperator,site,0).
                  result==Outcome::Applied);
            CHECK(shaped.world().workshop_trade_certified(second_id,WorkshopTrade::Shape));
            auto appoint_shape=workforce02::approved_workshop_supervisor(
                shaped.world(),7,ship,site,WorkshopTrade::Shape,second_id,741,true);
            CHECK(appoint_shape.has_value() && shaped.submit(*appoint_shape));
            shaped.step();
            CHECK(shaped.world().events().back().result==Outcome::Applied);
            auto shape_job=workforce02::approved_workshop_job(
                shaped.world(),7,ship,site,WorkshopTrade::Shape,1,742,true);
            CHECK(shape_job.has_value() && shaped.submit(*shape_job));
            shaped.step();
            CHECK(shaped.world().events().back().result==Outcome::Applied);
            const auto baseline=shaped.world().workshop(site)->finisher_batches;
            CHECK(shaped.world().workshop(site)->panels_kg>=21 ||
                  shaped.world().workshop(site)->reserved_panels_kg==20);
            auto practice=workforce02::approved_workshop_mentorship(
                shaped.world(),7,ship,site,WorkshopTrade::Shape,first_id,743,true);
            CHECK(practice.has_value() && shaped.submit(*practice));
            shaped.step();
            CHECK(shaped.world().events().back().result==Outcome::Applied &&
                  shaped.world().events().back().transferred==1);
            const auto& shape_credit=shaped.world().npc_records().at(first_id).
                industrial_training.back();
            CHECK(shape_credit.trade==WorkshopTrade::Shape &&
                  shape_credit.mentor_id==second_id &&
                  shape_credit.supervisor_appointment_id==741);
            CHECK(send(shaped,ship,744,Action::AssignWorkshopFinisher,site,
                       static_cast<std::int64_t>(first_id)).result==Outcome::Applied);
            shaped.steps(15);
            CHECK(shaped.world().workshop(site)->finisher_batches==baseline+1);
            shaped.steps(15);
            CHECK(shaped.world().workshop(site)->finisher_batches==baseline+1);
            const auto shape_board=workforce02::workshop_job_board(
                shaped.world(),7,ship,site);
            CHECK(shape_board[1].fulfilled && shape_board[1].physically_completed==1 &&
                  shape_board[1].approval_command==742 && !shape_board[1].suspended);
            CHECK(shaped.world().total_material_kg()==initial);
            const auto shape_replay=(folder/"wo2j_shape_recovered.sqlite").string();
            PersistentWorld::recover_copy(shape_db,shape_replay);
            PersistentWorld reopened(shape_replay,Simulation(1057));
            CHECK(reopened.world().workshop(site)->finisher_batches==baseline+1);
            CHECK(reopened.world().workshop(site)->approved_jobs[1].command_id==742);
            CHECK(reopened.world().npc_records().at(first_id).
                  industrial_training.back().supervisor_appointment_id==741);
            CHECK(reopened.world().total_material_kg()==initial);
        }
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
