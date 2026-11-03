#include "galaxy/RegionalFactoryHandoff.hpp"
#include "galaxy/NpcAppearanceGenome.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
namespace re=galaxy::regional_events;
namespace ind=galaxy::industry;
static void check(bool yes,const char* what){if(!yes)throw std::runtime_error(what);}
template<class F>static void reject(F f,const char* what){
    bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}
    check(rejected,what);
}
static ind::DurableFactory factory(EntityId home){
    ind::DurableFactory f;f.home_ship=home;f.workers={1,2};
    f.recipe={"aluminum-smelt","smelt",{{"ore",1000}},
              {"aluminum",950},{"tailings",50}};
    f.machine={1,"smelt",true,true,true};
    f.state.feed.capacity_grams=25000;f.state.feed.grams["ore"]=10000;
    f.state.finished.capacity_grams=25000;f.state.scrap.capacity_grams=25000;
    f.initial_mass=10000;return f;
}
static void region_start(re::CheckpointStore& region){
    re::Scheduler scheduler;
    cold_colony::State initial;initial.population=6;
    initial.habitat.food_kg=100;initial.habitat.water_kg=100;
    initial.habitat.oxygen_kg=100;
    scheduler.add_colony("home",initial);
    check(region.save(scheduler,0)==1,"regional checkpoint bootstrap failed");
}
static void advance_world(PersistentWorld& w,std::uint64_t ticks){
    for(std::uint64_t n=0;n<ticks;++n)w.step();
}
static bool state_equal(const ind::DurableFactory& a,const ind::DurableFactory& b){
    return a.state.elapsed_ticks==b.state.elapsed_ticks &&
        a.state.completed_batches==b.state.completed_batches &&
        a.state.ticks_remaining==b.state.ticks_remaining &&
        a.state.has_work_in_progress==b.state.has_work_in_progress &&
        a.state.feed.grams==b.state.feed.grams &&
        a.state.finished.grams==b.state.finished.grams &&
        a.state.scrap.grams==b.state.scrap.grams &&
        a.state.reserved_inputs.size()==b.state.reserved_inputs.size() &&
        (a.state.reserved_inputs.empty() ||
            a.state.reserved_inputs[0].grams==b.state.reserved_inputs[0].grams);
}
int main(){
    const auto file=(std::filesystem::temp_directory_path()/"sg_v50_real_factory_handoff.db").string();
    std::filesystem::remove(file);
    try{
        Simulation initial(9919);
        const EntityId home=initial.add_colony_ship(7,{0,0},100,6);
        const EntityId ferry=initial.add_colony_ship(7,{0,0},100,6);
        initial.enable_individual_workforce();initial.upgrade_assignment_snapshot_format();
        initial.enable_medical_workforce_rules();
        PersistentWorld world(file,std::move(initial));
        for(const auto& part:std::vector<std::pair<std::string,std::vector<std::uint64_t>>>{
                {"east",{1,2,3}},{"west",{4,5,6}}}){
            NpcAppearanceStore family(file,part.first,9919,[&](auto id){
                return world.world().npc_records().count(id)!=0;});
            family.register_settlement(home,3);
            for(const auto id:part.second)family.enroll_family_founder(id,home);
        }
        re::CheckpointStore region(file,"industrial-real");region_start(region);
        re::RegionalModeAuthority authority(region);
        check(authority.enroll().generation==1,"production regional owner missing");
        const auto scopes=std::vector<std::string>{"east","west"};
        ind::RegionalFactoryHandoff line(world,region,authority,file,"home",scopes);
        auto seed=factory(home);
        auto invalid=seed;invalid.workers={1,999};
        reject([&]{line.bootstrap(invalid,"invalid-staff");},"invented industry worker accepted");
        line.bootstrap(seed,"trusted-factory-stock-1");line.verify();
        reject([&]{(void)region.commit_command("unsafe-cold-scheduler","other-host",
            "advance-without-industry",region.load()->revision,
            [](re::Scheduler& s){s.advance_to(s.tick()+1);return std::uint64_t(0);});},
            "uncoordinated cold command bypassed industry material checkpoint");
        auto trial=authority.state();
        auto frozen=authority.prepare("unsafe-no-stage",re::SimulationOwner::Detailed,
            trial.generation,trial.checkpoint_revision,trial.boundary_tick);
        reject([&]{(void)authority.commit("unsafe-no-stage",frozen.generation,
            frozen.checkpoint_revision,[](sqlite3*,re::Scheduler&){});},
            "empty production promotion stage flipped owner without factory WIP");
        trial=authority.abort("unsafe-no-stage",frozen.generation);
        check(trial.owner==re::SimulationOwner::Cold&&
              trial.phase==re::TransitionPhase::Stable&&trial.generation==2,
              "abort did not durably rotate unsafe handoff token");
        check(line.active().state.feed.amount("ore")==10000,"host escrow not physically stocked");
        // Cold processing begins a genuine incomplete batch, preserving WIP.
        advance_world(world,2);
        reject([&]{(void)line.advance_cold(2,"cold-failed",
             ind::HandoffFailure::AfterEventWrite);},
             "cold factory event crash did not roll back world checkpoint and stock");
        check(region.load()->revision==1 &&
              line.active().state.feed.amount("ore")==10000,
              "failed cold effect leaked material or regional time");
        check(line.advance_cold(2,"cold-partial")==0,"unexpected early full batch");
        line.verify();
        check(line.active().state.has_work_in_progress &&
              line.active().state.ticks_remaining==3 &&
              line.active().state.feed.amount("ore")==9000,
              "cold WIP did not reserve actual ore");
        auto state=authority.state();
        const auto stale_gen=state.generation;
        reject([&]{(void)line.promote("handoff-1",ind::HandoffFailure::AfterFactoryWrite);},
              "promote failure did not abort DB transaction");
        check(authority.state().phase==re::TransitionPhase::ToDetailed,
              "failed promotion lost durable intent");
        check(line.active().state.ticks_remaining==3,
              "failed promotion mutated cold state");
        reject([&]{(void)line.advance_cold(1,"prepared-writer");},
              "prepared promotion allowed an ordinary cold factory tick");
        {PersistentWorld pending(file,Simulation(9919));
         re::CheckpointStore pending_store(file,"industrial-real");
         re::RegionalModeAuthority pending_owner(pending_store);
         ind::RegionalFactoryHandoff pending_line(pending,pending_store,pending_owner,
             file,"home",scopes);pending_line.verify();}
        state=line.promote("handoff-1");
        check(state.owner==re::SimulationOwner::Detailed &&
              state.phase==re::TransitionPhase::Stable &&
              state.generation>stale_gen,"promotion not actually owned by warm runtime");
        reject([&]{(void)line.advance_cold(1,"wrong-owner");},
              "cold writer ran under detailed owner");
        advance_world(world,3);
        reject([&]{(void)line.advance_detailed(3,"warm-crash",
            ind::HandoffFailure::AfterFactoryWrite);},
              "detailed half-written tick committed");
        check(line.active().state.ticks_remaining==3,
              "detailed rollback lost partial production cycle");
        reject([&]{(void)line.advance_detailed(3,"warm-event-crash",
             ind::HandoffFailure::AfterEventWrite);},
             "warm factory event crash left live production");
        check(line.active().state.ticks_remaining==3,
              "warm event crash leaked reserved inventory");
        check(line.advance_detailed(3,"warm-3")==1,"warm worker did not finish WIP batch");
        reject([&]{(void)line.advance_detailed(1,"factory-future-warm");},
               "detailed factory ran beyond last committed world tick");
        check(line.active().state.finished.amount("aluminum")==950 &&
              line.active().state.scrap.amount("tailings")==50,
              "warm material transformation broke recipe conservation");
        advance_world(world,7);
        check(line.advance_detailed(7,"warm-7")==1,"warm factory batch count wrong");
        line.verify();
        auto warm=line.active();
        check(warm.state.elapsed_ticks==12&&warm.state.has_work_in_progress&&
              warm.state.ticks_remaining==3&&warm.state.completed_batches==2,
              "real detailed tick/WIP not preserved");
        // An unrelated region may remain cold, using the identical quantified
        // line and REAL regional scheduler without duplicated material.
        re::CheckpointStore control(file,"industrial-control");region_start(control);
        re::RegionalModeAuthority control_authority(control);
        check(control_authority.enroll().generation==1,"control owner failed");
        ind::RegionalFactoryHandoff comparator(world,control,control_authority,file,"home",scopes);
        reject([&]{comparator.bootstrap(seed,"trusted-factory-stock-1");},
               "one source material receipt was admitted by two factory regions");
        comparator.bootstrap(seed,"trusted-control-stock");
        check(comparator.advance_cold(12,"control-cold-12")==2,
              "control cold vectorized production mismatch");
        comparator.verify();
        check(state_equal(warm,comparator.active()),
              "warm detailed and cold aggregate same time disagree");
        trial=authority.state();
        frozen=authority.prepare("unsafe-warm-flush",re::SimulationOwner::Cold,
            trial.generation,trial.checkpoint_revision,trial.boundary_tick);
        reject([&]{(void)authority.commit("unsafe-warm-flush",frozen.generation,
            frozen.checkpoint_revision,[](sqlite3*,re::Scheduler&){});},
            "empty warm demotion falsely committed detailed industry output");
        trial=authority.abort("unsafe-warm-flush",frozen.generation);
        check(trial.owner==re::SimulationOwner::Detailed &&
              trial.phase==re::TransitionPhase::Stable &&
              trial.generation==frozen.generation+1,
              "warm aborted demotion did not rotate owner token");
        line.verify(); // live warm materialization must inherit rotated token
        reject([&]{(void)line.demote("handoff-back",ind::HandoffFailure::AfterEventWrite);},
              "demotion journal fault committed partial cold output");
        check(authority.state().phase==re::TransitionPhase::ToCold,
              "interrupted demotion missing durable frozen intent");
        check(line.active().state.elapsed_ticks==12,
              "failed demotion changed active detailed output");
        state=line.demote("handoff-back");
        check(state.owner==re::SimulationOwner::Cold&&state.phase==re::TransitionPhase::Stable,
              "demotion failed to return exclusive cold ownership");
        check(region.load()->scheduler.tick()==12 &&
              state_equal(line.active(),comparator.active()),
              "demotion failed to reconcile production into cold checkpoint");
        reject([&]{(void)line.advance_detailed(1,"illegal-warm");},
              "warm worker accepted cold ownership");
        advance_world(world,3);
        check(line.advance_cold(3,"cold-finish")==1,
              "resumed cold worker did not finish detailed WIP");
        reject([&]{(void)line.advance_cold(1,"factory-future-cold");},
               "cold factory ran beyond last committed world tick");
        check(line.advance_cold(3,"cold-finish")==1,
              "duplicate cold command lost its durable batch count");
        check(line.active().state.completed_batches==3&&              line.active().state.finished.amount("aluminum")==2850&&
              line.active().state.scrap.amount("tailings")==150,
              "cold after warm did not conserve outputs");
        line.verify();
        state=line.promote("shift-availability");
        check(state.owner==re::SimulationOwner::Detailed,
              "factory workforce did not enter detailed mode");
        // Actual world passenger travel removes both assigned factory workers.
        // They remain registered at home but cannot provide imaginary shifts.
        Command board{};board.id=991;board.sequence=991;
        board.execute_tick=world.world().tick()+1;
        board.issuer=Simulation::kServerAuthority;
        board.action=Action::HostEmbarkPassengers;board.actor=ferry;board.target=home;
        board.transport_id=9901;board.passenger_ids={1,2};
        check(world.submit_host_passenger_action(board),"real workers could not embark");
        world.step();
        check(world.world().npc_records().at(1).location==ferry&&
              world.world().npc_records().at(2).location==ferry,
              "worker physical departure not committed");
        advance_world(world,4);
        const auto away=verify_regional_population(world,file,home,scopes,
            region.scope(),"home",region.load()->revision,authority.state().generation,false);
        check(away.registered==6 && away.named_away_or_transit==2 &&
              away.named_present_for_duty==4,
              "physical travel silently changed registered population or duty roster");
        const auto before_absence=line.active();
        check(line.advance_detailed(5,"absent-worker-warm")==0,
              "detailed factory produced with workers on a ferry");
        check(line.active().state.completed_batches==before_absence.state.completed_batches&&
              line.active().state.finished.grams==before_absence.state.finished.grams&&
              !line.active().machine.staffed,
              "physical workforce absence created free production");
        line.verify();
        Command arrive=board;arrive.id=992;arrive.sequence=992;
        arrive.execute_tick=world.world().tick()+1;
        arrive.action=Action::HostDisembarkPassengers;
        check(world.submit_host_passenger_action(arrive),"returning workers could not disembark");
        world.step();advance_world(world,4);
        check(line.advance_detailed(5,"worker-returned-warm")==1 &&
              line.active().machine.staffed,
              "returning named workers did not resume material-backed production");
        check(line.active().state.completed_batches==4,
              "worker roster return produced wrong batch count");
        line.verify();
        state=line.demote("shift-return-to-cold");
        check(state.owner==re::SimulationOwner::Cold&&
              region.load()->scheduler.tick()==25,
              "detailed workforce return was not reconciled into real cold scheduler");
        line.verify();
        // Real cold needs stock advances across the detailed interval exactly
        // as in an always-cold region, including the tick-20 ration boundary.
        check(comparator.advance_cold(13,"control-needs-25")>0,
              "independent cold control failed life-support catchup");
        const auto live=region.load()->scheduler.colony("home");
        const auto control_stock=control.load()->scheduler.colony("home");
        check(live.processed_tick==25 && control_stock.processed_tick==25 &&
              live.population==6 && control_stock.population==6 &&
              live.habitat.food_kg==94 && live.habitat.water_kg==94 &&
              live.habitat.oxygen_kg==94 &&
              live.habitat.food_kg==control_stock.habitat.food_kg &&
              live.habitat.water_kg==control_stock.habitat.water_kg &&
              live.habitat.oxygen_kg==control_stock.habitat.oxygen_kg &&
              live.habitat.organic_waste_kg==control_stock.habitat.organic_waste_kg &&
              live.habitat.wastewater_kg==control_stock.habitat.wastewater_kg &&
              live.habitat.co2_kg==control_stock.habitat.co2_kg,
              "regional warm/cold catchup invented or duplicated life-support resources");
        // Repeat the real travel under cold ownership: both modes must obey
        // exactly the same location-backed staffing admission.
        board.id=993;board.sequence=993;board.execute_tick=world.world().tick()+1;
        board.transport_id=9902;
        check(world.submit_host_passenger_action(board),
              "cold workforce absence embark failed");
        world.step();advance_world(world,4);
        const auto before_cold_absence=line.active();
        check(line.advance_cold(5,"cold-workforce-away")==0 &&
              line.active().state.completed_batches==before_cold_absence.state.completed_batches,
              "cold workforce time advance generated unstaffed batches");
        arrive=board;arrive.id=994;arrive.sequence=994;
        arrive.execute_tick=world.world().tick()+1;
        arrive.action=Action::HostDisembarkPassengers;
        check(world.submit_host_passenger_action(arrive),
              "cold worker return ferry arrival refused");
        world.step();advance_world(world,4);
        check(line.advance_cold(5,"cold-workforce-return")==1 &&
              line.active().state.completed_batches==5,
              "cold workforce did not resume after real return");
        line.verify();
        check(comparator.advance_cold(10,"control-needs-35")>0,
              "control region did not advance to common catch-up time");
        const auto live35=region.load()->scheduler.colony("home");
        const auto ctl35=control.load()->scheduler.colony("home");
        check(live35.processed_tick==35 && ctl35.processed_tick==35 &&
              live35.habitat.food_kg==ctl35.habitat.food_kg &&
              live35.habitat.water_kg==ctl35.habitat.water_kg &&
              live35.habitat.oxygen_kg==ctl35.habitat.oxygen_kg,
              "cold/detailed life support fell out of sync after workforce travel");
        {PersistentWorld reopened(file,Simulation(9919));
         re::CheckpointStore reopened_region(file,"industrial-real");
         re::RegionalModeAuthority reopened_authority(reopened_region);
         ind::RegionalFactoryHandoff reopened_line(reopened,reopened_region,
             reopened_authority,file,"home",scopes);
         reopened_line.verify();
         check(reopened_line.active().state.completed_batches==5,
               "restart lost real factory production");}
        // Fail closed on a forged material checkpoint and immutable effect.
        sqlite3* db=nullptr;
        check(sqlite3_open(file.c_str(),&db)==SQLITE_OK,"tamper DB open failed");
        check(sqlite3_exec(db,"UPDATE sg_v50_factory SET payload='invented aluminum' "
            "WHERE scope='industrial-real'",nullptr,nullptr,nullptr)==SQLITE_OK,
            "test could not corrupt factory head");sqlite3_close(db);
        reject([&]{line.verify();},"forged material record survived reconciliation");
        std::filesystem::remove(file);
        std::cout<<"PASS v50 real cold/detailed production, WIP, stock, staffing, owner CAS, rollback and restart\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL v50 real industry handoff: "<<e.what()<<'\n';return 1;
    }
}
