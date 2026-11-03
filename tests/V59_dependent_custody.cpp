#include "galaxy/MedicalSocietyBridge.hpp"
#include "galaxy/RegionalModeAuthority.hpp"
#include <sqlite3.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace galaxy;
namespace re=galaxy::regional_events;
static void check(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
template<class F> static void refused(F f,const char* why){
    bool caught=false;try{f();}catch(const std::exception&){caught=true;}
    check(caught,why);
}
static Command cmd(std::uint64_t id,const PersistentWorld& w,EntityId actor,
                   EntityId target,Action action,bool host=false){
    Command c{};c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
    c.issuer=host?Simulation::kServerAuthority:7;
    c.actor=actor;c.target=target;c.action=action;
    if(host){c.transport_id=900;c.passenger_ids={1};}
    return c;
}
static void execute(PersistentWorld& w,std::uint64_t id){
    w.step();check(!w.world().events().empty() && w.world().events().back().command_id==id &&
                   w.world().events().back().result==Outcome::Applied,
                   "physical voyage command not executed");
}
static void tamper(const std::string& path,const char* sql){
    sqlite3* db=nullptr;
    if(sqlite3_open_v2(path.c_str(),&db,SQLITE_OPEN_READWRITE,nullptr)!=SQLITE_OK)
        throw std::runtime_error("tamper open failed");
    char* error=nullptr;const int rc=sqlite3_exec(db,sql,nullptr,nullptr,&error);
    if(rc!=SQLITE_OK){const std::string why=error?error:"SQL mutation failed";
        sqlite3_free(error);sqlite3_close(db);throw std::runtime_error(why);}
    sqlite3_close(db);
}
int main(){
    const auto path=(std::filesystem::temp_directory_path()/"sg_q6e2_child_custody.db").string();
    const auto replay=path+".replay",bad=path+".bad",missing=path+".missing",
               downgraded=path+".downgraded", forged=path+".forged", orphan=path+".orphan",
               stripped=path+".stripped",flag=path+".flag";
    std::filesystem::remove(path);std::filesystem::remove(replay);
    std::filesystem::remove(bad);std::filesystem::remove(missing);
    std::filesystem::remove(downgraded);
    std::filesystem::remove(forged);std::filesystem::remove(orphan);
    std::filesystem::remove(stripped);std::filesystem::remove(flag);
    const std::uint64_t child=(1ULL<<62)+901, child2=(1ULL<<62)+902;
    EntityId origin=0,dest=0;
    try{
        {
            Simulation initial(4250);initial.add_system(2);
            origin=initial.add_colony_ship(7,{0,0},100,6);
            const auto ferry=initial.add_colony_ship(7,{0,0},100,6);
            dest=initial.add_colony_ship(7,{0,0},100,6);
            const auto gate=initial.add_gate(1,{0,0},2,{5000,0});
            initial.enable_individual_workforce();initial.upgrade_assignment_snapshot_format();
            initial.enable_medical_workforce_rules();
            PersistentWorld w(path,std::move(initial));
            NpcAppearanceStore family(path,"household",4250,[&](auto n){
                return w.world().npc_records().count(n)!=0;});
            family.register_settlement(origin,6);family.register_settlement(dest,6);
            family.enroll_family_founder(1,origin);family.enroll_family_founder(2,origin);
            family.enroll_family_founder(13,dest);
            re::CheckpointStore region(path,"household-region");re::Scheduler scheduler;
            cold_colony::State a{};a.population=6;
            scheduler.add_colony("origin",a);scheduler.add_colony("destination",a);
            check(region.save(scheduler,0)==1,"regional initialize failed");
            re::RegionalModeAuthority owner(region);
            check(owner.enroll().generation==1,"regional owner not enrolled");
            SocietyBirthPlan birth{};
            birth.expected_world_generation=w.generation();birth.expected_region_revision=1;
            birth.expected_owner_generation=1;birth.family_seed=4250;
            birth.event_id=90001;birth.child_id=child;birth.parent_a=1;birth.parent_b=2;
            birth.home_ship=origin;birth.family_scope="household";
            birth.region_scope="household-region";birth.regional_colony="origin";
            w.step_with_atomic_dependent_birth(birth);
            auto second_birth=birth;second_birth.expected_world_generation=w.generation();
            second_birth.expected_region_revision=2;second_birth.expected_family_revision=1;
            second_birth.event_id=90002;second_birth.child_id=child2;
            w.step_with_atomic_dependent_birth(second_birth);
            check(family.current_residence(child)==origin &&
                  family.current_residence(child2)==origin &&
                  family.family_census(origin).current_population==8 &&
                  region.load()->scheduler.colony("origin").population==8,
                  "dependent birth did not establish real census identity");
            check(w.submit(cmd(100,w,dest,gate,Action::RequestJump)),"destination jump refused");
            execute(w,100);w.steps(Simulation::kJumpTransitTicks+Simulation::kArrivalCloakTicks+1);
            check(w.submit_host_passenger_action(cmd(101,w,ferry,origin,Action::HostEmbarkPassengers,true)),"embark");
            DependentTravelPlan boarding{};
            boarding.expected_world_generation=w.generation();
            boarding.command_id=101;boarding.transport_id=900;
            boarding.guardian_id=1;boarding.family_seed=4250;
            boarding.ferry=ferry;boarding.origin=origin;boarding.destination=dest;
            boarding.family_scope="household";boarding.child_ids={child,child2};
            auto before_board=w.world().canonical_state();
            refused([&]{w.step_with_dependent_transport(boarding,FailPoint::AfterSnapshotWrite);},
                    "failed boarding did not abort all child berth effects");
            check(w.world().canonical_state()==before_board &&
                  w.dependent_travel("household").empty(),
                  "failed board advanced child ledger or physical passenger");
            {PersistentWorld reopened(path,Simulation(4250));
             check(reopened.dependent_travel("household").empty(),
                   "failed child boarding leaked across restart");}
            w.step_with_dependent_transport(boarding);
            check(w.dependent_travel("household").size()==2 &&
                  w.dependent_travel("household")[0].status==1 &&
                  w.dependent_travel("household")[1].status==1 &&
                  w.dependent_travel("household")[0].ferry==ferry,
                  "child physical custody differs from actual ferry");
            check(w.dependent_travel("household").size()==2 &&
                  w.dependent_travel("household")[0].status==1,
                  "children not physically aboard");
            check(w.world().npc_records().at(1).location==ferry,"guardian not aboard");
            check(w.submit(cmd(103,w,ferry,gate,Action::RequestJump)),"ferry jump");
            execute(w,103);
            const auto preflight=w.world().canonical_state();
            refused([&]{w.step(FailPoint::AfterSnapshotWrite);},
                    "failed in-flight tick unexpectedly committed");
            check(w.world().canonical_state()==preflight &&
                  w.dependent_travel("household")[0].status==1,
                  "child teleport/duplicate after failed transit tick");
            {PersistentWorld reopened(path,Simulation(4250));
             check(reopened.world().canonical_state()==preflight &&
                   reopened.dependent_travel("household")[0].status==1,
                   "child in-flight custody failed restart");}
            w.steps(Simulation::kJumpTransitTicks+Simulation::kArrivalCloakTicks+1);
            check(w.world().find(ferry)->system_id==2 &&
                  w.dependent_travel("household")[0].status==1,
                  "child physical custody did not follow real inter-system ship");
            check(w.submit_host_passenger_action(cmd(104,w,ferry,dest,Action::HostDisembarkPassengers,true)),"disembark");
            DependentTravelPlan arrival=boarding;
            arrival.boarding=false;arrival.command_id=104;
            arrival.expected_world_generation=w.generation();
            refused([&]{w.step();},"ordinary world tick bypassed dependent arrival effect");
            refused([&]{w.step_with_dependent_transport(arrival,FailPoint::AfterSnapshotWrite);},
                    "failed child disembarkation did not roll back");
            {PersistentWorld reopened(path,Simulation(4250));
             check(reopened.dependent_travel("household")[0].status==1 &&
                   reopened.world().npc_records().at(1).location==ferry,
                   "interrupted disembarkation lost child/guardian custody");}
            w.step_with_dependent_transport(arrival);
            check(w.dependent_travel("household")[0].status==2 &&
                  w.dependent_travel("household")[1].arrival_command==104,
                  "child voyage did not physically arrive");
            // Child occupies a real tracked berth but remains a non-worker census
            // identity, NOT a fabricated playable NPC.
            check(!w.world().npc_records().count(child),"dependent became a fake worker");
            check(family.current_residence(child)==origin,"child moved before legal settlement");
            const auto migrate=cmd(105,w,origin,dest,Action::HostSettleMigrant,true);
            check(w.submit_host_passenger_action(migrate),"guardian migration submission");
            SocietyMigrationPlan plan{};plan.expected_world_generation=w.generation();
            plan.command_id=105;plan.npc_id=1;plan.transport_id=900;
            plan.origin=origin;plan.destination=dest;
            plan.dependents={{child,90105},{child2,90106}};
            plan.expected_region_revision=3;plan.expected_owner_generation=1;
            plan.family_seed=4250;plan.expected_origin_family_revision=2;
            plan.expected_destination_family_revision=0;
            plan.family_scope="household";plan.region_scope="household-region";
            plan.origin_colony="origin";plan.destination_colony="destination";
            auto invalid=plan;invalid.dependents.clear();
            refused([&]{w.step_with_atomic_migration(invalid);},
                    "single-adult route bypassed guardian child obligation");
            invalid=plan;invalid.dependents={{child,105},{child2,90106}};
            refused([&]{w.step_with_atomic_migration(invalid);},"duplicate member event allowed");
            invalid=plan;invalid.dependents={{child+1,90105}};
            refused([&]{w.step_with_atomic_migration(invalid);},"invented dependent accepted");
            invalid=plan;invalid.expected_owner_generation=50;
            refused([&]{w.step_with_atomic_migration(invalid);},"stale owner allowed");
            const auto snapshot=w.world().canonical_state();
            refused([&]{w.step_with_atomic_migration(plan,FailPoint::AfterSnapshotWrite);},
                    "fault injection did not abort household");
            check(w.world().canonical_state()==snapshot &&
                  family.current_residence(child)==origin &&
                  family.current_residence(1)==origin &&
                  region.load()->revision==3 &&
                  family.family_census(origin).current_population==8,
                  "rolled back household leaked population or identity");
            w.step_with_atomic_migration(plan);
            check(w.world().npc_records().at(1).home_ship==dest &&
                  family.current_residence(1)==dest && family.current_residence(child)==dest &&
                  family.current_residence(child2)==dest,
                  "committed household residence not transferred");
            check(family.family_census(origin).current_population==5 &&
                  family.family_census(dest).current_population==9,
                  "family household population delta wrong");
            check(region.load()->revision==4 &&
                  region.load()->scheduler.colony("origin").population==5 &&
                  region.load()->scheduler.colony("destination").population==9,
                  "region household population delta wrong");
            check(family.get_birth(90001).parent_a==1 &&
                  family.get_birth(90001).parent_b==2 &&
                  family.get_move(90105).npc_id==child &&
                  family.get_move(90106).npc_id==child2,
                  "child immutable birth or move provenance lost");
            check(reconcile_medical_society(w,family,path,"household",origin,region,
                   "origin",4,w.generation()).reconciled &&
                  reconcile_medical_society(w,family,path,"household",dest,region,
                   "destination",4,w.generation()).dependent_births==2,
                  "post-move named/dependent society reconciliation refused");
            refused([&]{w.step_with_atomic_migration(plan);},"duplicate household replay admitted");
        }
        {
            PersistentWorld reopened(path,Simulation(4250));
            NpcAppearanceStore family(path,"household",4250,[&](auto n){
                return reopened.world().npc_records().count(n)!=0;});
            check(reopened.world().npc_records().at(1).home_ship==dest &&
                  family.current_residence(child)==dest &&
                  family.current_residence(child2)==dest,"restart duplicated or lost family custody");
        }
        PersistentWorld::recover_copy(path,replay);
        {
            PersistentWorld recovered(replay,Simulation(4250));
            NpcAppearanceStore family(replay,"household",4250,[&](auto n){
                return recovered.world().npc_records().count(n)!=0;});
            re::CheckpointStore region(replay,"household-region");
            check(family.current_residence(child)==dest &&
                  family.current_residence(child2)==dest &&
                  reconcile_medical_society(recovered,family,replay,"household",dest,
                    region,"destination",4,recovered.generation()).reconciled,
                  "genesis replay broke household settlement");
        }
        std::filesystem::copy_file(path,bad,std::filesystem::copy_options::overwrite_existing);
        tamper(bad,"DROP TRIGGER npc_family_move_no_update;"
                   "UPDATE npc_family_moves SET destination='999' WHERE event_id='90105'");
        refused([&]{PersistentWorld invalid(bad,Simulation(4250));},
                "forged dependent move not rejected on restore");
        std::filesystem::copy_file(path,missing,std::filesystem::copy_options::overwrite_existing);
        tamper(missing,"DELETE FROM sg_society_household_member WHERE event_id='90105'");
        refused([&]{PersistentWorld invalid(missing,Simulation(4250));},
                "missing child manifest not rejected on restore");
        std::filesystem::copy_file(path,downgraded,std::filesystem::copy_options::overwrite_existing);
        tamper(downgraded,"UPDATE sg_society_migration SET household_count=1 WHERE command_id='105'");
        refused([&]{PersistentWorld invalid(downgraded,Simulation(4250));},
                "downgraded household size was accepted");
        std::filesystem::copy_file(path,forged,std::filesystem::copy_options::overwrite_existing);
        tamper(forged,"UPDATE sg_dependent_travel SET arrival_command='9999' "
                      "WHERE child_id='4611686018427388805'");
        refused([&]{PersistentWorld invalid(forged,Simulation(4250));},
                "forged child arrival receipt accepted on restart");
        std::filesystem::copy_file(path,orphan,std::filesystem::copy_options::overwrite_existing);
        tamper(orphan,"DELETE FROM sg_dependent_transport_batch WHERE command_id='104'");
        refused([&]{PersistentWorld invalid(orphan,Simulation(4250));},
                "missing child arrival batch accepted on restart");
        std::filesystem::copy_file(path,stripped,std::filesystem::copy_options::overwrite_existing);
        tamper(stripped,"DROP TABLE sg_dependent_travel");
        refused([&]{PersistentWorld invalid(stripped,Simulation(4250));},
                "deleted physical passenger ledger passed migration replay");
        std::filesystem::copy_file(path,flag,std::filesystem::copy_options::overwrite_existing);
        tamper(flag,"UPDATE sg_society_migration SET physical_manifest=0 WHERE command_id='105'");
        refused([&]{PersistentWorld invalid(flag,Simulation(4250));},
                "downgraded child voyage certification passed restore");
        std::cout<<"PASS 05q6e2: two real child ferry berths, crash-safe departure/"
                   "arrival, one adult NPC, census-migration, journal replay and tamper refusal\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"05q6e failure: "<<e.what()<<'\n';return 1;
    }
}
