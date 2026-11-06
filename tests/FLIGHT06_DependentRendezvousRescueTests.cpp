#include "galaxy/MedicalSocietyBridge.hpp"
#include "galaxy/RegionalModeAuthority.hpp"
#include "galaxy/ShipInteractionExecutors.hpp"
#include <filesystem>
#include <sqlite3.h>
#include <string>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
namespace re=galaxy::regional_events;
static void ck(bool x,const char* msg){if(!x)throw std::runtime_error(msg);}
template<class F>static void denied(F f,const char* msg){bool bad=false;try{f();}catch(const std::exception&){bad=true;}ck(bad,msg);}
static Command cmd(const PersistentWorld& w,std::uint64_t id,Action action,
    EntityId actor,EntityId target,std::uint64_t voyage,std::vector<std::uint64_t> adults={1,2}){
    Command c{};c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
    c.action=action;c.actor=actor;c.target=target;c.transport_id=voyage;
    c.passenger_ids=std::move(adults);c.issuer=Simulation::kServerAuthority;return c;
}
static void backed_up(const std::string& source,const std::string& destination){
    std::filesystem::remove(destination);
    sqlite3 *src=nullptr,*dst=nullptr;
    if(sqlite3_open_v2(source.c_str(),&src,SQLITE_OPEN_READONLY,nullptr)!=SQLITE_OK ||
       sqlite3_open_v2(destination.c_str(),&dst,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE,
                       nullptr)!=SQLITE_OK)
        throw std::runtime_error("backup open failure");
    sqlite3_backup* backup=sqlite3_backup_init(dst,"main",src,"main");
    if(!backup)throw std::runtime_error("backup init failure");
    const int result=sqlite3_backup_step(backup,-1);
    const int finished=sqlite3_backup_finish(backup);
    sqlite3_close(dst);sqlite3_close(src);
    ck(result==SQLITE_DONE && finished==SQLITE_OK,"backup failed");
}
static void change(const std::string& dbpath,const char* sql){
    sqlite3* db=nullptr;
    if(sqlite3_open(dbpath.c_str(),&db)!=SQLITE_OK)
        throw std::runtime_error("offline tamper open failed");
    char* error=nullptr;
    const int result=sqlite3_exec(db,sql,nullptr,nullptr,&error);
    if(result!=SQLITE_OK){const std::string why=error?error:"offline sql failure";
        sqlite3_free(error);sqlite3_close(db);throw std::runtime_error(why);}
    sqlite3_close(db);
}
int main(){
    auto path=(std::filesystem::temp_directory_path()/"sg_flight06_dependent_rescue.db").string();
    std::filesystem::remove(path);
    const std::uint64_t child=(1ULL<<62)+661;
    try{
        Simulation initial(8661);
        auto origin=initial.add_colony_ship(7,{0,0},100,6);
        auto ferry=initial.add_colony_ship(7,{0,0},100,6);
        auto rescue=initial.add_colony_ship(7,{0,0},100,6);
        auto dest=initial.add_colony_ship(7,{0,0},100,6);
        initial.enable_individual_workforce();initial.upgrade_assignment_snapshot_format();
        initial.enable_medical_workforce_rules();
        PersistentWorld w(path,std::move(initial));
        NpcAppearanceStore family(path,"family-rescue",8661,[&](auto id){
            return w.world().npc_records().count(id)!=0;});
        family.register_settlement(origin,6);family.register_settlement(dest,6);
        family.enroll_family_founder(1,origin);family.enroll_family_founder(2,origin);
        re::CheckpointStore region(path,"family-rescue-region");
        re::Scheduler scheduler;cold_colony::State stock{};stock.population=6;
        scheduler.add_colony("A",stock);scheduler.add_colony("B",stock);
        ck(region.save(scheduler,0)==1,"checkpoint init");
        re::RegionalModeAuthority authority(region);ck(authority.enroll().generation==1,"owner");
        SocietyBirthPlan birth{};birth.expected_world_generation=w.generation();
        birth.expected_region_revision=1;birth.expected_owner_generation=1;
        birth.family_seed=8661;birth.event_id=77;birth.child_id=child;
        birth.parent_a=1;birth.parent_b=2;birth.home_ship=origin;
        birth.family_scope="family-rescue";birth.region_scope="family-rescue-region";
        birth.regional_colony="A";
        w.step_with_atomic_dependent_birth(birth);
        ck(w.submit_host_passenger_action(cmd(w,101,Action::HostEmbarkPassengers,
             ferry,origin,900)),"board command");
        DependentTravelPlan board{};board.expected_world_generation=w.generation();
        board.command_id=101;board.transport_id=900;board.guardian_id=1;
        board.co_guardian_id=2;board.family_seed=8661;board.expected_owner_generation=1;
        board.ferry=ferry;board.origin=origin;board.destination=dest;
        board.family_scope="family-rescue";board.region_scope="family-rescue-region";
        board.child_ids={child};w.step_with_dependent_transport(board);
        std::vector<std::uint64_t> everyone;
        for(const auto& [id,n]:w.world().npc_records())if(n.location==ferry)everyone.push_back(id);
        auto dangerous=cmd(w,102,Action::HostDestroyPassengerFerry,ferry,ferry,900,everyone);
        // Ensure the underlying ordinary kernel would allow this fully
        // manifested adult incident. The child SQL bridge must veto it.
        auto unsafe_without_child_ledger=w.world();
        ck(unsafe_without_child_ledger.submit(dangerous),
           "fatal incident fixture was invalid before child-specific guard");
        ck(!w.submit_host_passenger_action(dangerous),
           "child occupied ferry destruction must be refused before queueing");
        auto mixed=cmd(w,106,Action::HostResolveMixedFerryLoss,ferry,rescue,900,everyone);
        mixed.amount=2;
        auto adult_only=w.world();
        ck(adult_only.submit(mixed),"mixed incident fixture invalid before child guard");
        ck(!w.submit_host_passenger_action(mixed),
           "adult mixed-fatality command bypassed unaccounted child casualty guard");
        // FLIGHT-06: native named-NPC pilot must physically rendezvous with
        // the ferry before the host can execute any dependent custody change.
        ck(!galaxy::ship_interaction::execute_dependent_rescue_after_rendezvous(
            w,7,97061,DependentRescuePlan{}),
           "empty dependent rescue was accepted");
        const auto before_flight=w.world().canonical_state();
        bool fault=false;
        try{(void)w.queue_npc_ship_rendezvous(7,rescue,0,ferry,97061,
            NpcFlightPurpose::Rescue,FailPoint::AfterSnapshotWrite);}
        catch(const std::exception&){fault=true;}
        ck(fault && w.world().canonical_state()==before_flight,
           "failed rendezvous posting changed canonical custody");
        ck(w.queue_npc_ship_rendezvous(7,rescue,0,ferry,97061,
            NpcFlightPurpose::Rescue),"guardian rescue NPC flight refused");
        bool met=false;
        for(int i=0;i<400;i++){
            w.step();
            if(w.world().npc_flight_orders().at(97061).phase==
               NpcFlightPhase::Arrived){met=true;break;}
        }
        ck(met,"physical dependent rescue rendezvous did not complete");
        ck(galaxy::ship_interaction::rendezvous_preflight(
            w.world(),7,rescue,ferry,97061)==
            galaxy::ship_interaction::Readiness::Ready,
           "native guardian rescue is missing physical receipt");
        ck(w.world().voyage_status(900)==1,
           "rendezvous silently moved adult passengers");
        auto incident=cmd(w,103,Action::HostEvacuatePassengers,ferry,rescue,900);
        incident.amount=901;
        ck(!w.submit_host_passenger_action(incident),
           "unsafe generic rescue should not bypass minor custody");
        ck(w.submit_host_dependent_rescue_action(incident),"authorized child rescue not submitted");
        DependentRescuePlan plan{};plan.expected_world_generation=w.generation();
        plan.command_id=103;plan.original_transport=900;plan.rescue_transport=901;
        plan.guardian_id=1;plan.co_guardian_id=2;plan.family_seed=8661;
        plan.original_ferry=ferry;plan.rescue_ferry=rescue;plan.origin=origin;
        plan.destination=dest;plan.family_scope="family-rescue";
        plan.region_scope="family-rescue-region";plan.expected_owner_generation=1;
        plan.child_ids={child};
        const auto before=w.world().canonical_state();
        denied([&]{(void)galaxy::ship_interaction::execute_dependent_rescue_after_rendezvous(
             w,7,97061,plan,FailPoint::AfterSnapshotWrite);},
               "failed rescue commit leaked partial parent/child custody");
        ck(w.world().canonical_state()==before &&
           w.dependent_travel("family-rescue")[0].status==1,
           "aborted rescue changed committed child custody");
        {PersistentWorld reopened(path,Simulation(8661));
         ck(reopened.dependent_travel("family-rescue").size()==1,
            "aborted rescue persisted a duplicate child");}
        ck(!galaxy::ship_interaction::execute_dependent_rescue_after_rendezvous(
             w,8,97061,plan),"wrong-owner dependent rescue executed");
        ck(galaxy::ship_interaction::execute_dependent_rescue_after_rendezvous(
             w,7,97061,plan),"authorized full family rescue failed");
        auto legs=w.dependent_travel("family-rescue");
        ck(legs.size()==2 && legs[0].status==2 &&
           legs[0].termination_kind==1 && legs[1].status==1 &&
           legs[1].transfer_from_transport==900 && legs[1].ferry==rescue &&
           w.world().npc_records().at(1).transport_id==901 &&
           w.world().npc_records().at(2).transport_id==901,
           "child not in real, new rescue voyage with parents");
        auto state=authority.state();
        denied([&]{authority.prepare("warm-child",re::SimulationOwner::Detailed,
            state.generation,state.checkpoint_revision,state.boundary_tick);},
            "handoff lost active rescued child");
        {PersistentWorld reopened(path,Simulation(8661));
         ck(reopened.dependent_travel("family-rescue").size()==2 &&
            reopened.world().npc_records().at(1).location==rescue,
            "reopen lost child rescue chain");}
        auto leave=cmd(w,104,Action::HostDisembarkPassengers,rescue,dest,901);
        ck(w.submit_host_passenger_action(leave),"rescue arrival not submitted");
        auto arrival=board;arrival.boarding=false;arrival.expected_world_generation=w.generation();
        arrival.command_id=104;arrival.transport_id=901;arrival.ferry=rescue;
        w.step_with_dependent_transport(arrival);
        legs=w.dependent_travel("family-rescue");
        ck(legs.size()==2 && legs[1].status==2 && legs[1].termination_kind==0 &&
           w.world().npc_records().at(1).location==dest,
           "rescued child did not finally disembark");
        {PersistentWorld reopened(path,Simulation(8661));
         ck(reopened.dependent_travel("family-rescue")[1].status==2,
            "final rescue voyage did not survive restart");}
        const auto replay=path+".replay";
        std::filesystem::remove(replay);
        PersistentWorld::recover_copy(path,replay);
        {PersistentWorld recovered(replay,Simulation(8661));
         ck(recovered.dependent_travel("family-rescue").size()==2 &&
            recovered.world().canonical_state()==w.world().canonical_state(),
            "independent genesis replay lost physical child rescue custody");}
        const auto damaged=path+".damaged";
        backed_up(path,damaged);
        change(damaged,"UPDATE sg_dependent_travel SET transfer_from_transport='999' "
                       "WHERE transfer_from_transport='900'");
        denied([&]{PersistentWorld bad(damaged,Simulation(8661));},
               "forged source voyage in emergency rescue ledger was accepted");
        backed_up(path,damaged);
        change(damaged,"DELETE FROM sg_dependent_transport_batch WHERE phase=3");
        denied([&]{PersistentWorld bad(damaged,Simulation(8661));},
               "deleted emergency transfer batch was accepted");
        backed_up(path,damaged);
        change(damaged,"DROP TABLE sg_dependent_travel");
        denied([&]{PersistentWorld bad(damaged,Simulation(8661));},
               "dropping child custody ledger after rescue was accepted");
        backed_up(path,damaged);
        change(damaged,"DELETE FROM sg_dependent_travel");
        denied([&]{PersistentWorld bad(damaged,Simulation(8661));},
               "empty child custody ledger after rescue was accepted");
        std::filesystem::remove(replay);std::filesystem::remove(damaged);
        std::filesystem::remove(path);
        std::cout<<"PASS FLIGHT-06: physical rendezvous, whole-family rescue, SQLite crash, recovery and tamper refusal\n";
    }catch(const std::exception& e){std::cerr<<"FAIL dependent rescue: "<<e.what()<<'\n';return 1;}
}
