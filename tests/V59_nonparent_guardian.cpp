#include "galaxy/MedicalSocietyBridge.hpp"
#include "galaxy/RegionalModeAuthority.hpp"
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
    EntityId actor,EntityId target,std::uint64_t voyage,std::vector<std::uint64_t> adults={1,2,3}){
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
    auto path=(std::filesystem::temp_directory_path()/"sg_q6e5b3_nonparent_guardian.db").string();
    std::filesystem::remove(path);
    const std::uint64_t child=(1ULL<<62)+661,child2=(1ULL<<62)+671;
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
        auto sibling=birth;
        sibling.expected_world_generation=w.generation();
        sibling.expected_region_revision=2;
        sibling.expected_family_revision=1;
        sibling.event_id=78;sibling.child_id=child2;
        w.step_with_atomic_dependent_birth(sibling);
        ck(w.submit_host_passenger_action(cmd(w,101,Action::HostEmbarkPassengers,
             ferry,origin,900)),"board command");
        DependentTravelPlan board{};board.expected_world_generation=w.generation();
        board.command_id=101;board.transport_id=900;board.guardian_id=1;
        board.co_guardian_id=2;board.family_seed=8661;board.expected_owner_generation=1;
        board.ferry=ferry;board.origin=origin;board.destination=dest;
        board.family_scope="family-rescue";board.region_scope="family-rescue-region";
        board.child_ids={child,child2};w.step_with_dependent_transport(board);
        const auto first=w.audit_dependent_incident("family-rescue",900,{child,child2});
        ck(first.aboard==2 && first.arrived==0 && first.ever_rescued==0 &&
           first.members.size()==2 &&
           first.members[0].custody==DependentVerifiedCustody::Aboard &&
           first.members[0].guardian_id==1 && first.members[0].co_guardian_id==2,
           "original child voyage not accounted for");
        denied([&]{(void)w.audit_dependent_incident("family-rescue",900,{});},
               "empty incident roster was accepted");
        denied([&]{(void)w.audit_dependent_incident("family-rescue",900,{child+1});},
               "false incident passenger roster was accepted");
        denied([&]{(void)w.audit_dependent_incident("family-rescue",900,{child,child});},
               "duplicate incident passenger was accepted");
        // A stranded ferry retains physical child custody across a restart.
        auto strand=cmd(w,201,Action::HostStrandFerry,ferry,ferry,900,{1,2,3});
        strand.amount=2;
        ck(w.submit_host_passenger_action(strand),"strand action refused");
        w.step();
        {PersistentWorld reloaded(path,Simulation(8661));
         const auto stranded=reloaded.audit_dependent_incident("family-rescue",900,{child,child2});
         ck(stranded.stranded==2 && stranded.members[0].current_ferry==ferry,
            "stranded child was not retained after restart");}
        DependentStrandingCasePlan docket{};
        docket.case_id="case-original-201";docket.family_scope="family-rescue";
        docket.original_transport=900;docket.current_transport=900;
        docket.ferry=ferry;docket.stranding_command_id=201;
        docket.expected_world_generation=w.generation();
        docket.adult_ids={1,2,3};docket.child_ids={child,child2};
        denied([&]{w.record_dependent_stranding_case(docket,FailPoint::AfterReceiptWrite);},
               "aborted journal header wrote a partial case");
        denied([&]{w.record_dependent_stranding_case(docket,FailPoint::AfterSnapshotWrite);},
               "aborted journal manifest wrote a partial case");
        {PersistentWorld reloaded(path,Simulation(8661));
         ck(reloaded.audit_dependent_incident("family-rescue",900,{child,child2}).stranded==2,
            "incident header failpoint changed physical custody");}
        auto forged=docket;forged.case_id="case-fake-adults";forged.adult_ids={1};
        denied([&]{w.record_dependent_stranding_case(forged);},
               "incident accepted incomplete real adult roster");
        forged=docket;forged.case_id="case-fake-child";forged.child_ids={child+1};
        denied([&]{w.record_dependent_stranding_case(forged);},
               "incident accepted invented child berth");
        forged=docket;forged.case_id="case-fake-incident";forged.stranding_command_id=999;
        denied([&]{w.record_dependent_stranding_case(forged);},
               "incident accepted unjournaled stranding");
        w.record_dependent_stranding_case(docket);
        denied([&]{w.record_dependent_stranding_case(docket);},
               "incident accepted duplicate historical case");
        {PersistentWorld reopened(path,Simulation(8661));
         ck(reopened.world().voyage_status(900)==3,
            "committing incident case unexpectedly moved stranded ferry");}

        ck(w.submit_host_passenger_action(cmd(w,202,Action::HostResumeFerry,
             ferry,ferry,900,{1,2,3})),"resume action refused");
        w.step();
        ck(w.audit_dependent_incident("family-rescue",900,{child,child2}).aboard==2,
           "resumed child voyage not accounted for");

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
        plan.child_ids={child,child2};
        const auto before=w.world().canonical_state();
        denied([&]{w.step_with_dependent_rescue(plan,FailPoint::AfterSnapshotWrite);},
               "failed rescue commit leaked partial parent/child custody");
        ck(w.world().canonical_state()==before &&
           w.dependent_travel("family-rescue")[0].status==1,
           "aborted rescue changed committed child custody");
        {PersistentWorld reopened(path,Simulation(8661));
         ck(reopened.dependent_travel("family-rescue").size()==2,
            "aborted rescue persisted a duplicate child");}
        w.step_with_dependent_rescue(plan);
        // Outcome is a separate, immutable historical attestation of the actual
        // rescue. It does NOT create a second rescue or change legal parentage.
        DependentRescueOutcomePlan adjudication{};
        adjudication.outcome_id="rescue-outcome-103";
        adjudication.case_id="case-original-201";
        adjudication.family_scope="family-rescue";
        adjudication.expected_world_generation=w.generation();
        adjudication.rescue_command_id=103;
        adjudication.source_transport=900;adjudication.rescue_transport=901;
        adjudication.receiving_ferry=rescue;adjudication.receiving_guardian_id=2;
        adjudication.child_ids={child,child2};
        denied([&]{w.record_dependent_rescue_outcome(adjudication,
               FailPoint::AfterReceiptWrite);},"failed rescue outcome header leaked");
        denied([&]{w.record_dependent_rescue_outcome(adjudication,
               FailPoint::AfterSnapshotWrite);},"failed rescue outcome members leaked");
        {PersistentWorld reopened(path,Simulation(8661));
         denied([&]{(void)reopened.verify_dependent_rescue_outcome(
                  adjudication.outcome_id);},"aborted rescue outcome was visible");}
        auto bad_outcome=adjudication;
        bad_outcome.receiving_guardian_id=3;
        denied([&]{w.record_dependent_rescue_outcome(bad_outcome);},
               "unmanifested receiving guardian accepted");
        bad_outcome=adjudication;bad_outcome.child_ids={child+1};
        denied([&]{w.record_dependent_rescue_outcome(bad_outcome);},
               "invented child rescue outcome accepted");
        bad_outcome=adjudication;bad_outcome.rescue_command_id=202;
        denied([&]{w.record_dependent_rescue_outcome(bad_outcome);},
               "resume event accepted as evacuation");
        bad_outcome=adjudication;bad_outcome.source_transport=12345;
        denied([&]{w.record_dependent_rescue_outcome(bad_outcome);},
               "forged source voyage accepted");
        w.record_dependent_rescue_outcome(adjudication);
        denied([&]{w.record_dependent_rescue_outcome(adjudication);},
               "duplicate rescue outcome was accepted");
        const auto outcome=w.verify_dependent_rescue_outcome(adjudication.outcome_id);
        ck(outcome.receiving_guardian_id==2 && outcome.receiving_transport==901 &&
           outcome.child_ids==std::vector<std::uint64_t>{child,child2} &&
           outcome.current_custody.rescued_aboard==2,
           "recorded rescue outcome lacks verified receiving parent or berth");
        {PersistentWorld reloaded(path,Simulation(8661));
         ck(reloaded.verify_dependent_rescue_outcome(adjudication.outcome_id)
             .current_custody.ever_rescued==2,
             "rescue outcome lost parent custody on restart");}
        // Non-parent (existing NPC #3) is a REAL receiving-ferry passenger.
        // Both original parents must authorize the exact child roster. Neither
        // approval alone changes legal custody.
        NonParentGuardianApprovalPlan a{};
        a.authorization_id="parent-consent-a";
        a.rescue_outcome_id=adjudication.outcome_id;
        a.family_scope="family-rescue";
        a.expected_world_generation=w.generation();
        a.approving_parent_id=1;a.receiving_guardian_id=3;
        a.child_ids={child,child2};
        denied([&]{w.record_nonparent_guardian_approval(a,FailPoint::AfterReceiptWrite);},
               "partial parent approval was committed");
        denied([&]{w.record_nonparent_guardian_approval(a,FailPoint::AfterSnapshotWrite);},
               "partial approval metadata was committed");
        {PersistentWorld reload(path,Simulation(8661));
         ck(reload.verify_dependent_rescue_outcome(adjudication.outcome_id).child_ids.size()==2,
            "guardian approval failure broke rescue outcome");}
        auto false_parent=a;false_parent.authorization_id="false-parent";
        false_parent.approving_parent_id=4;
        denied([&]{w.record_nonparent_guardian_approval(false_parent);},
               "invented birth parent approved non-parent guardian");
        auto false_child=a;false_child.authorization_id="false-child";
        false_child.child_ids={child+1};
        denied([&]{w.record_nonparent_guardian_approval(false_child);},
               "incomplete or invented child guardian roster approved");
        w.record_nonparent_guardian_approval(a);
        NonParentGuardianshipPlan transfer{};
        transfer.transfer_id="legal-transfer-103";
        transfer.rescue_outcome_id=adjudication.outcome_id;
        transfer.family_scope="family-rescue";
        transfer.expected_world_generation=w.generation();
        transfer.receiving_guardian_id=3;transfer.child_ids={child,child2};
        transfer.authorizations={"parent-consent-a"};
        denied([&]{w.settle_nonparent_guardianship(transfer);},
               "single parent bypassed dual-parent legal consent");
        auto b=a;b.authorization_id="parent-consent-b";b.approving_parent_id=2;
        w.record_nonparent_guardian_approval(b);
        transfer.authorizations={"parent-consent-a","parent-consent-b"};
        denied([&]{w.settle_nonparent_guardianship(transfer,FailPoint::AfterReceiptWrite);},
               "nonparent guardian header failure leaked legal custody");
        denied([&]{w.settle_nonparent_guardianship(transfer,FailPoint::AfterSnapshotWrite);},
               "nonparent guardian member failure leaked legal custody");
        {NpcAppearanceStore family_after(path,"family-rescue",8661,[&](auto id){
             return w.world().npc_records().count(id)!=0;});
         ck(family_after.current_nonparent_guardian(child)==0 &&
            family_after.current_nonparent_guardian(child2)==0,
            "failed guardianship commit changed family guardian");}
        w.settle_nonparent_guardianship(transfer);
        denied([&]{w.settle_nonparent_guardianship(transfer);},
               "duplicate nonparent guardianship settled");
        const auto verified=w.verify_nonparent_guardianship(transfer.transfer_id);
        ck(verified.guardian_id==3&&verified.child_ids==std::vector<std::uint64_t>{child,child2},
           "verified nonparent guardian receipt absent");
        {NpcAppearanceStore family_after(path,"family-rescue",8661,[&](auto id){
             return w.world().npc_records().count(id)!=0;});
         ck(family_after.current_nonparent_guardian(child)==3 &&
            family_after.current_nonparent_guardian(child2)==3,
            "ART02 current nonparent guardian not persisted");}
        {PersistentWorld reload(path,Simulation(8661));
         ck(reload.verify_nonparent_guardianship(transfer.transfer_id).guardian_id==3,
            "new legal guardian lost during database restart");}
        const auto rescue_audit=w.audit_dependent_incident("family-rescue",900,{child,child2});
        ck(rescue_audit.rescued_aboard==2 && rescue_audit.ever_rescued==2 &&
           rescue_audit.members[0].current_transport==901 &&
           rescue_audit.members[0].current_ferry==rescue &&
           rescue_audit.members[0].custody==DependentVerifiedCustody::RescuedAboard,
           "evacuated child is not accounted for on receiving ferry");

        // Rescue ferries can also be immobilized. This must not be hidden
        // behind a generic "rescued" label in the read-only audit.
        auto stranded_rescue=cmd(w,203,Action::HostStrandFerry,
                                 rescue,rescue,901,{1,2,3});
        stranded_rescue.amount=2;
        ck(w.submit_host_passenger_action(stranded_rescue),
           "rescue ferry stranding refused");
        w.step();
        const auto stranded_again=w.audit_dependent_incident("family-rescue",900,{child,child2});
        ck(stranded_again.rescued_stranded==2 && stranded_again.ever_rescued==2 &&
           stranded_again.members[0].custody==DependentVerifiedCustody::RescuedStranded,
           "stranded rescue ship was reported as safe underway");
        ck(w.verify_dependent_rescue_outcome(adjudication.outcome_id)
           .current_custody.rescued_stranded==2,
           "historical rescue attestation hid current rescue-vessel stranding");
        auto rescue_case=docket;
        rescue_case.case_id="case-rescue-203";
        rescue_case.expected_world_generation=w.generation();
        rescue_case.current_transport=901;rescue_case.ferry=rescue;
        rescue_case.stranding_command_id=203;
        w.record_dependent_stranding_case(rescue_case);
        {PersistentWorld reloaded(path,Simulation(8661));
         ck(reloaded.audit_dependent_incident("family-rescue",900,{child,child2}).rescued_stranded==2,
            "rescue incident journal could not reopen");}

        ck(w.submit_host_passenger_action(cmd(w,204,Action::HostResumeFerry,
               rescue,rescue,901,{1,2,3})),"rescue ferry resume refused");
        w.step();
        ck(w.audit_dependent_incident("family-rescue",900,{child,child2}).rescued_aboard==2,
           "rescued family did not resume after second stranding");
        auto legs=w.dependent_travel("family-rescue");
        ck(legs.size()==4 && legs[0].status==2 &&
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
         ck(reopened.dependent_travel("family-rescue").size()==4 &&
            reopened.world().npc_records().at(1).location==rescue,
            "reopen lost child rescue chain");}
        auto leave=cmd(w,104,Action::HostDisembarkPassengers,rescue,dest,901);
        ck(w.submit_host_passenger_action(leave),"rescue arrival not submitted");
        auto arrival=board;arrival.boarding=false;arrival.expected_world_generation=w.generation();
        arrival.command_id=104;arrival.transport_id=901;arrival.ferry=rescue;
        w.step_with_dependent_transport(arrival);
        const auto final_audit=w.audit_dependent_incident("family-rescue",900,{child,child2});
        ck(final_audit.arrived==2 && final_audit.ever_rescued==2 &&
           final_audit.members[0].custody==DependentVerifiedCustody::Arrived,
           "rescued child lacks final safe arrival");
        ck(w.verify_dependent_rescue_outcome(adjudication.outcome_id)
           .current_custody.arrived==2,
           "historical rescue outcome failed to follow final safe arrival");

        legs=w.dependent_travel("family-rescue");
        ck(legs.size()==4 && legs[1].status==2 && legs[1].termination_kind==0 &&
           w.world().npc_records().at(1).location==dest,
           "rescued child did not finally disembark");
        {PersistentWorld reopened(path,Simulation(8661));
         ck(reopened.dependent_travel("family-rescue")[1].status==2,
            "final rescue voyage did not survive restart");}
        const auto replay=path+".replay";
        std::filesystem::remove(replay);
        PersistentWorld::recover_copy(path,replay);
        {PersistentWorld recovered(replay,Simulation(8661));
         ck(recovered.verify_dependent_rescue_outcome(adjudication.outcome_id)
             .current_custody.arrived==2,
             "genesis recovery did not verify rescue outcome and guardian");
         ck(recovered.audit_dependent_incident("family-rescue",900,{child,child2}).arrived==2,
            "historical recovery did not reconstruct incident custody");
         ck(recovered.verify_nonparent_guardianship("legal-transfer-103").guardian_id==3,
            "independent journal recovery lost non-parent legal custodian");

         ck(recovered.dependent_travel("family-rescue").size()==4 &&
            recovered.world().canonical_state()==w.world().canonical_state(),
            "independent genesis replay lost physical child rescue custody");}
        // Offline corruption must never silently replace or delete non-parent
        // guardianship, independent parent approvals, or historical checkpoints.
        const auto forged_guardian=path+".bad-legal-guardian";
        backed_up(path,forged_guardian);
        change(forged_guardian,"UPDATE world_meta SET dependent_guardian_transfer_count=0");
        denied([&]{PersistentWorld invalid(forged_guardian,Simulation(8661));},
               "guardian transfer was not anchored to world metadata");
        backed_up(path,forged_guardian);
        change(forged_guardian,"DROP TRIGGER npc_family_guardianship_immutable_delete; "
              "DELETE FROM npc_family_guardianship");
        denied([&]{PersistentWorld invalid(forged_guardian,Simulation(8661));},
               "orphaned legal guardian transfer survived family receipt deletion");
        backed_up(path,forged_guardian);
        change(forged_guardian,"DROP TRIGGER sg_guardian_approval_immutable_update; "
              "UPDATE sg_guardian_approval SET receiving_guardian='4' "
              "WHERE authorization_id='parent-consent-b'");
        denied([&]{PersistentWorld invalid(forged_guardian,Simulation(8661));},
               "forged parental approval recipient was accepted");
        backed_up(path,forged_guardian);
        change(forged_guardian,"DROP TABLE sg_guardian_approval");
        denied([&]{PersistentWorld invalid(forged_guardian,Simulation(8661));},
               "deleted parental authorization ledger was silently accepted");
        backed_up(path,forged_guardian);
        change(forged_guardian,"DELETE FROM snapshots WHERE generation IN "
              "(SELECT approved_generation FROM sg_guardian_approval)");
        denied([&]{PersistentWorld invalid(forged_guardian,Simulation(8661));},
               "missing pinned parental consent snapshot went unnoticed");
        // Even a replay copy must independently reject the forged guardian.
        backed_up(path,forged_guardian);
        change(forged_guardian,"UPDATE world_meta SET dependent_guardian_approval_count=0");
        denied([&]{PersistentWorld::recover_copy(forged_guardian,path+".forged-replay");},
               "genesis replay certified a forged authorization ledger");
        std::filesystem::remove(forged_guardian);
        // Outcome receipts must remain independently verifiable after a
        // later safe arrival and snapshot pruning.
        const auto damaged_outcome=path+".bad-outcome";
        backed_up(path,damaged_outcome);
        change(damaged_outcome,"DROP TRIGGER sg_dependent_rescue_outcome_immutable_update; "
              "UPDATE sg_dependent_rescue_outcome SET receiving_guardian='3'");
        denied([&]{PersistentWorld bad(damaged_outcome,Simulation(8661));},
               "forged receiving guardian was accepted after restart");
        backed_up(path,damaged_outcome);
        change(damaged_outcome,"DROP TRIGGER sg_dependent_rescue_outcome_member_immutable_delete; "
              "DELETE FROM sg_dependent_rescue_outcome_member");
        denied([&]{PersistentWorld bad(damaged_outcome,Simulation(8661));},
               "deleted child rescue outcome receipt was accepted");
        backed_up(path,damaged_outcome);
        change(damaged_outcome,"DROP TABLE sg_dependent_rescue_outcome");
        denied([&]{PersistentWorld bad(damaged_outcome,Simulation(8661));},
               "missing activated rescue outcome ledger was accepted");
        backed_up(path,damaged_outcome);
        change(damaged_outcome,"UPDATE world_meta SET dependent_rescue_outcome_count=0");
        denied([&]{PersistentWorld bad(damaged_outcome,Simulation(8661));},
               "forged rescue outcome count anchor was accepted");
        backed_up(path,damaged_outcome);
        change(damaged_outcome,"DELETE FROM snapshots WHERE generation IN "
              "(SELECT rescue_generation FROM sg_dependent_rescue_outcome)");
        denied([&]{PersistentWorld bad(damaged_outcome,Simulation(8661));},
               "missing rescue historical snapshot was accepted");
        backed_up(path,damaged_outcome);
        change(damaged_outcome,"DROP TRIGGER sg_dependent_rescue_outcome_immutable_update; "
              "UPDATE sg_dependent_rescue_outcome SET rescue_command='204'");
        denied([&]{PersistentWorld bad(damaged_outcome,Simulation(8661));},
               "forged rescue command accepted");
        const auto damaged_replay=damaged_outcome+".replay";
        std::filesystem::remove(damaged_replay);
        denied([&]{PersistentWorld::recover_copy(damaged_outcome,damaged_replay);},
               "recover_copy certified altered rescue outcome");
        std::filesystem::remove(damaged_outcome);
        // Offline tampering must be detected on reopen even after the two
        // ferries resume and the child safely reaches the destination.
        const auto missing_case=path+".missing-case";
        backed_up(path,missing_case);
        change(missing_case,"DROP TRIGGER sg_dependent_incident_case_member_immutable_delete; "
              "DELETE FROM sg_dependent_incident_case_member WHERE kind=2");
        denied([&]{PersistentWorld forged_world(missing_case,Simulation(8661));},
               "deleted historical child manifest not detected on recovery");
        const auto missing_header=path+".missing-header";
        backed_up(path,missing_header);
        change(missing_header,"DROP TABLE sg_dependent_incident_case");
        denied([&]{PersistentWorld forged_world(missing_header,Simulation(8661));},
               "deleted activated incident ledger was silently accepted");
        const auto forged_count=path+".forged-count";
        backed_up(path,forged_count);
        change(forged_count,"UPDATE world_meta SET dependent_case_count=0");
        denied([&]{PersistentWorld forged_world(forged_count,Simulation(8661));},
               "altered incident anchor counter was silently accepted");
        const auto forged_cause=path+".forged-cause";
        backed_up(path,forged_cause);
        change(forged_cause,"DROP TRIGGER sg_dependent_incident_case_immutable_update; "
              "UPDATE sg_dependent_incident_case SET stranding_command='204' "
              "WHERE case_id='case-rescue-203'");
        denied([&]{PersistentWorld forged_world(forged_cause,Simulation(8661));},
               "invalid stranding cause was silently accepted");
        const auto forged_origin=path+".forged-origin";
        backed_up(path,forged_origin);
        change(forged_origin,"DROP TRIGGER sg_dependent_incident_case_immutable_update; "
              "UPDATE sg_dependent_incident_case SET original_transport='12345' "
              "WHERE case_id='case-rescue-203'");
        denied([&]{PersistentWorld forged_world(forged_origin,Simulation(8661));},
               "forged original ferry leg was accepted after rescue");
        const auto missing_proof=path+".missing-proof";
        backed_up(path,missing_proof);
        change(missing_proof,"DELETE FROM snapshots WHERE generation IN "
              "(SELECT observed_generation FROM sg_dependent_incident_case)");
        denied([&]{PersistentWorld forged_world(missing_proof,Simulation(8661));},
               "missing pinned historical snapshots went undetected");
        const auto repaired=missing_case+".attempted-recovery";
        std::filesystem::remove(repaired);
        denied([&]{PersistentWorld::recover_copy(missing_case,repaired);},
               "genesis recovery claimed success with missing dependent manifest");
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
        std::cout<<"PASS two children, 3 adults, trusted nonparent approvals, crash and corruption refusal\n";
    }catch(const std::exception& e){std::cerr<<"FAIL dependent rescue: "<<e.what()<<'\n';return 1;}
}
