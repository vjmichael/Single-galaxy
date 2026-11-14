#include "galaxy/MedicalSocietyBridge.hpp"
#include "galaxy/Stage07HouseholdLifecycle.hpp"
#include "galaxy/RegionalModeAuthority.hpp"
#include <sqlite3.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace galaxy;
namespace re=galaxy::regional_events;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class F> static void reject(F&& f,const char* message){bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed,message);}
static Command command(PersistentWorld& w,std::uint64_t id,Action action,
                       EntityId actor,EntityId target,std::uint64_t voyage=0,
                       std::vector<std::uint64_t> people={}){
    Command c{};c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
    c.issuer=voyage?Simulation::kServerAuthority:7;
    c.action=action;c.actor=actor;c.target=target;
    c.transport_id=voyage;c.passenger_ids=std::move(people);return c;
}
static void step_applied(PersistentWorld& w,std::uint64_t id){
    w.step();check(w.world().events().back().command_id==id &&
        w.world().events().back().result==Outcome::Applied,"expected world action did not apply");
}
int main(){
    auto path=(std::filesystem::temp_directory_path()/"sg_q6e4_paired_settlement.db").string();
    std::filesystem::remove(path);
    const std::uint64_t kid1=(1ULL<<62)+8801,kid2=(1ULL<<62)+8802;
    EntityId source=0,destination=0,ferry=0;
    try{
        Simulation initial(8800);initial.add_system(2);
        source=initial.add_colony_ship(7,{0,0},100,6);
        ferry=initial.add_colony_ship(7,{0,0},100,6);
        destination=initial.add_colony_ship(7,{0,0},100,6);
        auto gate=initial.add_gate(1,{0,0},2,{5000,0});
        initial.enable_individual_workforce();initial.upgrade_assignment_snapshot_format();
        initial.enable_medical_workforce_rules();
        PersistentWorld world(path,std::move(initial));
        NpcAppearanceStore family(path,"paired",8800,[&](auto npc){
            return world.world().npc_records().count(npc)!=0;});
        family.register_settlement(source,6);family.register_settlement(destination,6);
        family.enroll_family_founder(1,source);family.enroll_family_founder(2,source);
        re::CheckpointStore regional(path,"paired-region");re::Scheduler scheduler;
        cold_colony::State stock{};stock.population=6;
        scheduler.add_colony("source",stock);scheduler.add_colony("destination",stock);
        check(regional.save(scheduler,0)==1,"checkpoint init");
        re::RegionalModeAuthority auth(regional);
        check(auth.enroll().generation==1,"authority enrollment");
        SocietyBirthPlan birth{};birth.expected_world_generation=world.generation();
        birth.expected_region_revision=1;birth.expected_owner_generation=1;
        birth.family_seed=8800;birth.event_id=2001;birth.child_id=kid1;
        birth.parent_a=1;birth.parent_b=2;birth.home_ship=source;
        birth.family_scope="paired";birth.region_scope="paired-region";
        birth.regional_colony="source";
        world.step_with_atomic_dependent_birth(birth);
        birth.expected_world_generation=world.generation();
        birth.expected_region_revision=2;birth.expected_family_revision=1;
        birth.event_id=2002;birth.child_id=kid2;
        world.step_with_atomic_dependent_birth(birth);
        {
            const auto life=stage7::audit_household_lifecycle(
                world,family,path,{2001,2002});
            check(life.unregistered_births==2 &&
                  life.registered_children==0 &&
                  life.original_census.named_living==18 &&
                  life.children.at(kid1).legal_home==source,
                  "Stage 7 birth cohort was cloned into physical labor");
        }
        check(world.submit(command(world,20,Action::RequestJump,destination,gate)),
              "destination gate jump submit");step_applied(world,20);
        world.steps(Simulation::kJumpTransitTicks+Simulation::kArrivalCloakTicks+1);
        auto boarding=command(world,21,Action::HostEmbarkPassengers,ferry,source,900,{1,2});
        check(world.submit_host_passenger_action(boarding),"two-parent boarding submission");
        DependentTravelPlan plan{};plan.expected_world_generation=world.generation();
        plan.command_id=21;plan.transport_id=900;plan.guardian_id=1;
        plan.co_guardian_id=2;plan.expected_owner_generation=1;
        plan.family_seed=8800;plan.ferry=ferry;plan.origin=source;
        plan.destination=destination;plan.family_scope="paired";
        plan.region_scope="paired-region";plan.child_ids={kid1,kid2};
        auto bad=plan;bad.co_guardian_id=3;
        reject([&]{world.step_with_dependent_transport(bad);},
               "unmanifested second parent accepted");
        bad=plan;bad.expected_owner_generation=4;
        reject([&]{world.step_with_dependent_transport(bad);},
               "stale regional owner accepted");
        // SQLite failpoint must not leave half-boarded dependent berths.
        const auto before=world.world().canonical_state();
        reject([&]{world.step_with_dependent_transport(plan,FailPoint::AfterSnapshotWrite);},
               "failed parent boarding not rolled back");
        check(before==world.world().canonical_state() &&
              world.dependent_travel("paired").empty(),"partial family boarding leaked");
        world.step_with_dependent_transport(plan);
        const auto legs=world.dependent_travel("paired");
        check(legs.size()==2 && legs[0].co_guardian_id==2 && legs[1].co_guardian_id==2 &&
              legs[0].status==1 && world.world().npc_records().at(1).location==ferry &&
              world.world().npc_records().at(2).location==ferry,"physical paired ferry proof missing");
        {PersistentWorld reopened(path,Simulation(8800));
         check(reopened.dependent_travel("paired")[0].co_guardian_id==2,
               "reopen lost second-parent berth lineage");}
        // An inter-region ownership switch cannot happen with dependent berths occupied.
        auto state=auth.state();
        reject([&]{auth.prepare("paired-detailed",re::SimulationOwner::Detailed,
                  state.generation,state.checkpoint_revision,state.boundary_tick);},
               "handoff allowed children to vanish from in-flight ownership");
        check(auth.state().phase==re::TransitionPhase::Stable,
              "rejected handoff leaked prepared state");
        // Child stranding/resumption is nonfatal and retains the exact same voyage.
        auto strand=command(world,22,Action::HostStrandFerry,ferry,ferry,900,{1,2});
        strand.amount=1;
        check(world.submit_host_passenger_action(strand),"stranding submit");step_applied(world,22);
        check(world.dependent_travel("paired")[0].status==1,
              "stranding removed child from ferry");
        auto resumed=command(world,23,Action::HostResumeFerry,ferry,ferry,900,{1,2});
        check(world.submit_host_passenger_action(resumed),"resume submit");step_applied(world,23);
        check(world.world().voyage_status(900)==1,"resumed voyage not active");
        check(world.submit(command(world,24,Action::RequestJump,ferry,gate)),"ferry jump");
        step_applied(world,24);
        auto in_flight=world.world().canonical_state();
        reject([&]{world.step(FailPoint::AfterSnapshotWrite);},
               "failed in-flight tick unexpectedly committed");
        {PersistentWorld reopened(path,Simulation(8800));
         check(reopened.world().canonical_state()==in_flight &&
               reopened.dependent_travel("paired")[1].status==1,
               "crash stranded or duplicated family");}
        world.steps(Simulation::kJumpTransitTicks+Simulation::kArrivalCloakTicks+1);
        auto arrive=command(world,25,Action::HostDisembarkPassengers,
                            ferry,destination,900,{1,2});
        check(world.submit_host_passenger_action(arrive),"two-parent arrival submit");
        plan.boarding=false;plan.command_id=25;
        plan.expected_world_generation=world.generation();
        reject([&]{world.step_with_dependent_transport(plan,FailPoint::AfterSnapshotWrite);},
               "interrupted arrival created orphaned child");
        world.step_with_dependent_transport(plan);
        check(world.dependent_travel("paired")[0].status==2 &&
              world.world().npc_records().at(1).location==destination &&
              world.world().npc_records().at(2).location==destination &&
              family.current_residence(kid1)==source,
              "children did not physically arrive or changed legal home prematurely");
        // DEMOG-05q6e4: the two adults and both physically arrived dependents
        // must acquire ONE legal household home under a single committed step.
        auto settle=command(world,26,Action::HostSettleMigrant,
                            source,destination,900,{1,2});
        check(world.submit_host_passenger_action(settle),"paired residence command rejected");
        SocietyMigrationPlan house{};
        house.expected_world_generation=world.generation();
        house.command_id=26;house.npc_id=1;house.co_parent_id=2;
        house.co_parent_event_id=90026;house.transport_id=900;
        house.origin=source;house.destination=destination;
        house.dependents={{kid1,90027},{kid2,90028}};
        house.expected_region_revision=3;house.expected_owner_generation=1;
        house.expected_origin_family_revision=2;
        house.expected_destination_family_revision=0;
        house.family_seed=8800;house.family_scope="paired";
        house.region_scope="paired-region";
        house.origin_colony="source";house.destination_colony="destination";
        auto bad_house=house;bad_house.co_parent_event_id=26;
        reject([&]{world.step_with_atomic_migration(bad_house);},
               "duplicate co-parent move event admitted");
        bad_house=house;bad_house.co_parent_id=3;
        reject([&]{world.step_with_atomic_migration(bad_house);},
               "wrong second parent accepted");
        bad_house=house;bad_house.expected_owner_generation=999;
        reject([&]{world.step_with_atomic_migration(bad_house);},
               "stale cold owner accepted");
        // Real owner handoff preparation must freeze household immigration.
        auto owner_now=auth.state();
        auto frozen=auth.prepare("paired-home-freeze",re::SimulationOwner::Detailed,
                    owner_now.generation,owner_now.checkpoint_revision,owner_now.boundary_tick);
        reject([&]{world.step_with_atomic_migration(house);},
               "prepared cold/warm handoff admitted whole-family move");
        auto unfrozen=auth.abort("paired-home-freeze",frozen.generation);
        check(unfrozen.owner==re::SimulationOwner::Cold &&
              unfrozen.phase==re::TransitionPhase::Stable &&
              unfrozen.generation==2,"abort did not rotate owner token");
        house.expected_owner_generation=unfrozen.generation;
        auto prior_world=world.world().canonical_state();
        reject([&]{world.step_with_atomic_migration(house,FailPoint::AfterSnapshotWrite);},
               "injected failure did not roll back both parents");
        check(world.world().canonical_state()==prior_world &&
              family.current_residence(1)==source &&
              family.current_residence(2)==source &&
              family.current_residence(kid1)==source &&
              regional.load()->revision==3,
              "failed household settlement leaked partial state");
        world.step_with_atomic_migration(house);
        check(world.world().npc_records().at(1).home_ship==destination &&
              world.world().npc_records().at(2).home_ship==destination &&
              family.current_residence(1)==destination &&
              family.current_residence(2)==destination &&
              family.current_residence(kid1)==destination &&
              family.current_residence(kid2)==destination &&
              family.family_census(source).current_population==4 &&
              family.family_census(destination).current_population==10 &&
              regional.load()->scheduler.colony("source").population==4 &&
              regional.load()->scheduler.colony("destination").population==10 &&
              world.world().colony_ship(source)->crew==4 &&
              world.world().colony_ship(destination)->crew==8,
              "paired household population or custody did not reconcile");
        {
            const auto life=stage7::audit_household_lifecycle(
                world,family,path,{2001,2002});
            check(life.relocated_children==2 &&
                  life.children.at(kid1).legal_home==destination &&
                  life.children.at(kid2).phase==
                      stage7::ChildPhase::LegallyRelocated &&
                  life.original_census.named_living==18,
                  "Stage 7 two-parent legal migration failed lifecycle audit");
        }
        check(family.get_move(90026).npc_id==2,
              "second parent has no durable family move receipt");
        {PersistentWorld reopened(path,Simulation(8800));
         check(reopened.world().npc_records().at(1).home_ship==destination &&
               reopened.world().npc_records().at(2).home_ship==destination,
               "restart lost paired legal home");}
        // With all passengers ashore, owner may stage an immutable cold->detailed
        // transition and later a reconciled detailed->cold transition.
        state=auth.state();
        auto prepared=auth.prepare("paired-detailed",re::SimulationOwner::Detailed,
                  state.generation,state.checkpoint_revision,state.boundary_tick);
        check(prepared.phase==re::TransitionPhase::ToDetailed,"warm promotion not prepared");
        auto detailed=auth.commit("paired-detailed",prepared.generation,
                  prepared.checkpoint_revision,[](sqlite3* db,re::Scheduler&){
            char* err=nullptr;
            int rc=sqlite3_exec(db,"CREATE TABLE IF NOT EXISTS paired_stage "
                   "(id INTEGER PRIMARY KEY, note TEXT NOT NULL); "
                   "INSERT INTO paired_stage VALUES(1,'promoted')",nullptr,nullptr,&err);
            if(rc!=SQLITE_OK){std::string message=err?err:"stage SQL";sqlite3_free(err);
                throw std::runtime_error(message);}
        });
        check(detailed.owner==re::SimulationOwner::Detailed &&
              detailed.phase==re::TransitionPhase::Stable,"detailed handoff failed");
        auto to_cold=auth.prepare("paired-cold",re::SimulationOwner::Cold,
                 detailed.generation,detailed.checkpoint_revision,detailed.boundary_tick);
        auto cold=auth.commit("paired-cold",to_cold.generation,
                 to_cold.checkpoint_revision,[](sqlite3*,re::Scheduler&){});
        check(cold.owner==re::SimulationOwner::Cold &&
              cold.generation==4,"stable cold authority not recovered");
        {PersistentWorld reopened(path,Simulation(8800));
         check(reopened.dependent_travel("paired").size()==2 &&
               reopened.dependent_travel("paired")[0].status==2,
               "cold-warm roundtrip destroyed dependent arrival");}
        // Deliberately forge the second parent's immutable family-event link.
        // Historical restoration must refuse the otherwise valid checkpoint.
        sqlite3* bad_db=nullptr;
        check(sqlite3_open_v2(path.c_str(),&bad_db,SQLITE_OPEN_READWRITE,nullptr)==SQLITE_OK,
              "could not open society receipt for corruption test");
        char* sql_error=nullptr;
        const auto sql_rc=sqlite3_exec(bad_db,
            "UPDATE sg_society_migration SET co_parent_event='999999' "
            "WHERE command_id='26'",nullptr,nullptr,&sql_error);
        if(sql_rc!=SQLITE_OK){
            const std::string why=sql_error?sql_error:"SQL tamper failed";
            sqlite3_free(sql_error);sqlite3_close(bad_db);
            throw std::runtime_error(why);
        }
        sqlite3_close(bad_db);
        reject([&]{PersistentWorld invalid(path,Simulation(8800));},
               "forged second-parent event accepted on restart");
        std::filesystem::remove(path);
        std::cout<<"PASS q6e4 two real parents, two children, crash-safe travel, cold/warm fences\n";
    }catch(const std::exception& e){std::cerr<<"FAIL q6e3a: "<<e.what()<<'\n';return 1;}
}
