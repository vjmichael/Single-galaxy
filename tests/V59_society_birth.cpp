#include "galaxy/MedicalSocietyBridge.hpp"
#include "galaxy/RegionalModeAuthority.hpp"
#include <sqlite3.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace galaxy;
namespace re=galaxy::regional_events;
static void check(bool condition,const char* reason){if(!condition)throw std::runtime_error(reason);}
template<class Fn>static void refused(Fn fn,const char* reason){
    bool thrown=false;try{fn();}catch(const std::exception&){thrown=true;}
    check(thrown,reason);
}
static void tamper(const std::string& filename,const char* sql){
    sqlite3* db=nullptr;
    check(sqlite3_open_v2(filename.c_str(),&db,SQLITE_OPEN_READWRITE,nullptr)==SQLITE_OK,
          "tamper fixture database unavailable");
    char* error=nullptr;
    const int rc=sqlite3_exec(db,sql,nullptr,nullptr,&error);
    if(rc!=SQLITE_OK){const std::string msg=error?error:"tamper fixture failed";
        sqlite3_free(error);sqlite3_close(db);throw std::runtime_error(msg);}
    sqlite3_close(db);
}
int main(){
    auto path=(std::filesystem::temp_directory_path()/"sg_q6c_atomic_birth.db").string();
    auto replay=path+".replay", broken_region=path+".region", broken_family=path+".family";
    std::filesystem::remove(path);std::filesystem::remove(replay);
    std::filesystem::remove(broken_region);std::filesystem::remove(broken_family);
    try{
        std::uint64_t home=0;
        const auto child1=(1ULL<<62)+401, child2=(1ULL<<62)+402;
        {
            Simulation initial(4041);
            home=initial.add_colony_ship(7,{0,0},100,12);
            initial.enable_individual_workforce();
            initial.upgrade_assignment_snapshot_format();
            initial.enable_medical_workforce_rules();
            PersistentWorld world(path,std::move(initial));
            NpcAppearanceStore family(path,"q6c-family",4041,[&](std::uint64_t id){
                return world.world().npc_records().count(id)!=0;});
            family.register_settlement(home,12);
            family.enroll_family_founder(1,home);
            family.enroll_family_founder(2,home);
            re::CheckpointStore region(path,"q6c-region");
            re::Scheduler scheduler;
            cold_colony::State baseline{};baseline.population=12;
            scheduler.add_colony("home",baseline);
            check(region.save(scheduler,0)==1,"no initial cold regional population");
            re::RegionalModeAuthority authority(region);
            check(authority.enroll().generation==1,"regional owner enrollment failed");
            SocietyBirthPlan plan{};
            plan.expected_world_generation=world.generation();
            plan.expected_region_revision=1;plan.expected_owner_generation=1;
            plan.family_seed=4041;plan.expected_family_revision=0;
            plan.event_id=91001;plan.child_id=child1;
            plan.parent_a=1;plan.parent_b=2;plan.home_ship=home;
            plan.family_scope="q6c-family";plan.region_scope="q6c-region";
            plan.regional_colony="home";
            auto wrong=plan;wrong.parent_b=99;
            refused([&]{world.step_with_atomic_dependent_birth(wrong);},
                    "nonexistent parent allowed atomic birth");
            check(world.generation()==1,"failed parent check mutated world");
            // Parent 4 exists physically but is not a registered ART02 family
            // founder: the attempted birth must roll back the world step.
            wrong=plan;wrong.parent_b=4;
            refused([&]{world.step_with_atomic_dependent_birth(wrong);},
                    "unregistered lineage was accepted");
            check(world.generation()==1 && family.family_census(home).current_population==12 &&
                  region.load()->revision==1,
                  "invalid lineage partially mutated regional or world population");
            world.step_with_atomic_dependent_birth(plan);
            check(world.generation()==2 && world.world().tick()==1,
                  "birth not witnessed by real world step");
            check(!world.world().npc_records().count(child1),
                  "cohort-only child improperly materialized as live NPC");
            check(family.family_census(home).current_population==13,
                  "atomic birth did not increment ART02 census");
            const auto birth=family.get_birth(91001);
            check(birth.child_id==child1 && birth.parent_a==1 && birth.parent_b==2 &&
                  birth.census_revision==1,"immutable family birth ledger missing");
            const auto cp=region.load();
            check(cp && cp->revision==2 && cp->scheduler.colony("home").population==13,
                  "atomic regional birth population did not reconcile");
            const auto projection=reconcile_medical_society(world,family,path,"q6c-family",home,
                    region,"home",2,2);
            check(projection.reconciled && projection.living_named==12 &&
                  projection.dependent_births==1 && projection.family_population==13,
                  "birth created a fake worker or lost dependent census");
            // A prepared cold-to-warm handoff freezes family/regional mutation.
            const auto prep=authority.prepare("q6c-promotion",re::SimulationOwner::Detailed,
                                              1,2,cp->scheduler.tick());
            check(prep.phase==re::TransitionPhase::ToDetailed,
                  "prepared authority did not freeze cold owner");
            auto next=plan;next.expected_world_generation=2;
            next.expected_region_revision=2;next.expected_family_revision=1;
            next.event_id=91002;next.child_id=child2;
            refused([&]{world.step_with_atomic_dependent_birth(next);},
                    "birth admitted during prepared warm handoff");
            check(world.generation()==2 && family.family_census(home).current_population==13,
                  "handoff freeze leaked partial birth");
            const auto unfreeze=authority.abort("q6c-promotion",1);
            check(unfreeze.generation==2 && unfreeze.phase==re::TransitionPhase::Stable,
                  "handoff abort did not invalidate owner token");
            refused([&]{world.step_with_atomic_dependent_birth(next);},
                    "stale owner token allowed cohort birth");
            next.expected_owner_generation=unfreeze.generation;
            refused([&]{world.step_with_atomic_dependent_birth(next,FailPoint::AfterSnapshotWrite);},
                    "injected post-effect failure was not propagated");
            check(world.generation()==2 && family.family_census(home).current_population==13 &&
                  region.load()->revision==2,"failed birth write leaked into other stores");
            refused([&]{(void)family.get_birth(91002);},
                    "rolled-back birth left a family identity behind");
            world.step_with_atomic_dependent_birth(next);
            check(world.generation()==3 && world.world().tick()==2 &&
                  family.family_census(home).current_population==14 &&
                  region.load()->revision==3 &&
                  region.load()->scheduler.colony("home").population==14,
                  "authorized same-event retry did not commit exactly once");
            refused([&]{world.step_with_atomic_dependent_birth(next);},
                    "old generation allowed duplicate birth replay");
            auto duplicate=next;duplicate.expected_world_generation=3;
            duplicate.expected_region_revision=3;duplicate.expected_family_revision=2;
            duplicate.event_id=91003;
            refused([&]{world.step_with_atomic_dependent_birth(duplicate);},
                    "duplicate child identity was admitted");
        }
        {
            Simulation ignored(4041);
            PersistentWorld reopened(path,std::move(ignored));
            NpcAppearanceStore family(path,"q6c-family",4041,[&](auto id){
                return reopened.world().npc_records().count(id)!=0;});
            re::CheckpointStore region(path,"q6c-region");
            const auto projection=reconcile_medical_society(reopened,family,path,"q6c-family",
                home,region,"home",3,reopened.generation());
            check(projection.reconciled && projection.dependent_births==2 &&
                  projection.family_population==14,"restart lost two atomic reserved births");
        }
        PersistentWorld::recover_copy(path,replay);
        {
            Simulation ignored(4041);
            PersistentWorld recovered(replay,std::move(ignored));
            NpcAppearanceStore family(replay,"q6c-family",4041,[&](auto id){
                return recovered.world().npc_records().count(id)!=0;});
            re::CheckpointStore region(replay,"q6c-region");
            check(reconcile_medical_society(recovered,family,replay,"q6c-family",home,
                region,"home",3,recovered.generation()).dependent_births==2,
                "independent genesis replay lost family/region birth custody");
        }
        tamper(replay,"UPDATE sg_med_society_birth SET parent_b='99' WHERE event_id='91001'");
        refused([&]{Simulation ignored(4041);PersistentWorld invalid(replay,std::move(ignored));},
                "forged birth lineage was accepted on restore");
        PersistentWorld::recover_copy(path,broken_region);
        tamper(broken_region,"UPDATE sg_regional_command_receipt SET request='forged' "
               "WHERE actor='society-birth-host' AND command_id='91001'");
        refused([&]{Simulation ignored(4041);PersistentWorld invalid(broken_region,std::move(ignored));},
                "forged regional birth receipt was accepted on restore");
        PersistentWorld::recover_copy(path,broken_family);
        // Model an offline attacker who first removes an immutability trigger.
        tamper(broken_family,"DROP TRIGGER npc_family_birth_no_delete; "
               "DELETE FROM npc_family_births WHERE event_id='91002'");
        refused([&]{Simulation ignored(4041);PersistentWorld invalid(broken_family,std::move(ignored));},
                "missing child family receipt was accepted on restore");
        std::cout<<"DEMOG-05q6c: atomic dependent births, handoff freeze, rollback, "
                    "restart, replay and tamper checks PASS\\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"DEMOG-05q6c failure: "<<e.what()<<'\n';
        return 1;
    }
}
