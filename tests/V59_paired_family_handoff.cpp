#include "galaxy/MedicalSocietyBridge.hpp"
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
    auto path=(std::filesystem::temp_directory_path()/"sg_q6e3a_paired_handoff.db").string();
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
              cold.generation==3,"stable cold authority not recovered");
        {PersistentWorld reopened(path,Simulation(8800));
         check(reopened.dependent_travel("paired").size()==2 &&
               reopened.dependent_travel("paired")[0].status==2,
               "cold-warm roundtrip destroyed dependent arrival");}
        std::filesystem::remove(path);
        std::cout<<"PASS q6e3a two real parents, two children, crash-safe travel, cold/warm fences\n";
    }catch(const std::exception& e){std::cerr<<"FAIL q6e3a: "<<e.what()<<'\n';return 1;}
}
