#include "galaxy/Persistence.hpp"
#include "galaxy/NpcAppearanceGenome.hpp"
#include "galaxy/RegionalModeAuthority.hpp"
#include "galaxy/ColonySafety.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("AUTO04D5B: ")+ #x+" line "+std::to_string(__LINE__));}while(false)
static void sql(sqlite3* db,const std::string& command){
    char* err=nullptr;
    if(sqlite3_exec(db,command.c_str(),nullptr,nullptr,&err)!=SQLITE_OK){
        const std::string why=err?err:"unknown";sqlite3_free(err);
        throw std::runtime_error("fixture SQL: "+why);
    }
}
static void clean(const std::string& path){
    for(const auto* suffix:{"","-wal","-shm"})std::filesystem::remove(path+suffix);
}
int main(){
try{
    const auto db=(std::filesystem::temp_directory_path()/
       ("sg_npc_supervisor_v79_"+std::to_string(std::chrono::steady_clock::now()
           .time_since_epoch().count())+".sqlite")).string();
    const auto recovered=db+".recovered",tampered=db+".tampered";
    constexpr std::uint64_t seed=7979;
    const auto child=(1ULL<<62)+7979;
    const std::string scope="v79-guardians",region_scope="reg-79";
    EntityId source=0,far=0,near=0;
    std::int64_t original_material=0;
    PersistentWorld::AutonomousSupervisorRoute route{};
    {
        Simulation sim(7979);
        source=sim.add_colony_ship(7,{0,0},100,2);
        far=sim.add_colony_ship(7,{1500,0},100,2);
        near=sim.add_colony_ship(7,{0,0},100,2);
        sim.provision_colony_genesis(source,100,100,100,200,500);
        sim.provision_colony_genesis(far,1000,1000,1000,1000,1000);
        sim.provision_colony_genesis(near,1000,1000,1000,1000,1000);
        sim.enable_individual_workforce();
        sim.upgrade_assignment_snapshot_format();
        sim.enable_passenger_life_support();
        original_material=sim.total_material_kg();
        PersistentWorld world(db,std::move(sim));
        {
            NpcAppearanceStore family(db,scope,seed,
                [&](std::uint64_t id){return world.world().npc_records().count(id)!=0;});
            family.register_settlement(source,2);
            family.register_settlement(far,2);
            family.register_settlement(near,2);
            family.enroll_family_founder(1,source);family.enroll_family_founder(2,source);
            family.enroll_family_founder(3,far);family.enroll_family_founder(4,far);
            family.enroll_family_founder(5,near);family.enroll_family_founder(6,near);
        }
        namespace re=galaxy::regional_events;
        re::CheckpointStore region(db,region_scope);
        re::Scheduler scheduler;
        cold_colony::State base{};base.population=2;
        scheduler.add_colony("source",base);
        scheduler.add_colony("far",base);
        scheduler.add_colony("near",base);
        CHECK(region.save(scheduler,0)==1);
        re::RegionalModeAuthority authority(region);
        CHECK(authority.enroll().generation==1);
        SocietyBirthPlan birth{};
        birth.expected_world_generation=world.generation();
        birth.expected_region_revision=1;
        birth.expected_owner_generation=1;
        birth.family_seed=seed;birth.expected_family_revision=0;
        birth.event_id=7971;birth.child_id=child;
        birth.parent_a=1;birth.parent_b=2;birth.home_ship=source;
        birth.family_scope=scope;birth.region_scope=region_scope;
        birth.regional_colony="source";
        world.step_with_atomic_dependent_birth(birth);
        Command register_child{};
        register_child.id=7972;register_child.sequence=7972;
        register_child.issuer=Simulation::kServerAuthority;
        register_child.actor=source;register_child.target=source;
        register_child.action=Action::HostRegisterDependent;
        register_child.transport_id=child;register_child.passenger_ids={1};
        register_child.execute_tick=world.world().tick()+1;
        CHECK(world.submit_host_dependent_registration(register_child,scope,seed));
        world.step_with_dependent_registration();
        route.source=source;route.family_seed=seed;
        route.intent_id=7973;route.command_id=7974;
        route.family_scope=scope;route.region_scope=region_scope;
        route.origin_colony="source";
        route.destinations={{far,"far"},{near,"near"}};
        route.member_event_ids={7981,7982,7983};
        const auto safe=world.supervise_autonomous_colony_emergency(route);
        CHECK(safe.status==PersistentWorld::SupervisorStatus::NoCurrentEmergency);
        CHECK(!world.pending_autonomous_family_rescue(route.command_id));
        world.steps(640-world.world().tick());
        CHECK(static_cast<int>(colony_safety::assess(world.world(),source,source).risk)>=
              static_cast<int>(colony_safety::Risk::Critical));
        CHECK(world.world().children_physically_aboard(source)==1);
        const auto pending_notifications=world.pending_colony_notifications(100);
        CHECK(!pending_notifications.empty());
        auto missing=route;
        missing.destinations={{far,"nonexistent-colony"}};
        const auto distress=world.supervise_autonomous_colony_emergency(missing);
        CHECK(distress.status==PersistentWorld::SupervisorStatus::AwaitingPhysicalRescue);
        CHECK(distress.leader_id==1);
        CHECK(distress.notice_id!=0);
        CHECK(world.colony_notification_acknowledgement_count()>0);
        CHECK(world.generation()>1);
        const auto at=world.generation();
        CHECK(world.world().colony_ship(source)->crew==2);
        CHECK(world.world().children_physically_aboard(source)==1);
        bool crashed=false;
        try{(void)world.supervise_autonomous_colony_emergency(
            route,FailPoint::AfterSnapshotWrite);}
        catch(const std::runtime_error&){crashed=true;}
        CHECK(crashed);
        CHECK(world.generation()==at);
        CHECK(!world.pending_autonomous_family_rescue(route.command_id));
        CHECK(!world.world().has_command_id(route.command_id));
        const auto queued=world.supervise_autonomous_colony_emergency(route);
        CHECK(queued.status==PersistentWorld::SupervisorStatus::RescueQueued);
        CHECK(queued.leader_id==1);
        CHECK(queued.notice_id==distress.notice_id);
        CHECK(queued.receiver==near);
        CHECK(world.generation()==at+1);
        CHECK(!world.verified_atomic_family_departure(route.command_id));
        CHECK(world.world().children_physically_aboard(source)==1);
        auto plan=world.pending_autonomous_family_rescue(route.command_id);
        CHECK(plan.has_value());
        CHECK(plan->family.adult_ids==std::vector<std::uint64_t>({1,2}));
        CHECK(plan->family.children.size()==1);
        CHECK(plan->family.children[0].child_id==child);
        CHECK(plan->family.children[0].guardian_id==1);
        CHECK(plan->family.receiver==near);
        CHECK(plan->member_event_ids==route.member_event_ids);
    }
    // Simulate process termination with the command admitted and the entire
    // family physically still at the source. Restore with NO host plan.
    {
        PersistentWorld reopened(db,Simulation(7991));
        CHECK(reopened.world().children_physically_aboard(source)==1);
        const auto pending=reopened.pending_autonomous_family_rescue(route.command_id);
        CHECK(pending.has_value());
        CHECK(pending->family.receiver==near);
        // A supervisor polling another source/region must never seize this
        // family's in-flight command just because the SQL queue is global.
        auto wrong_colony=route;
        wrong_colony.source=far;
        const auto fenced=reopened.supervise_autonomous_colony_emergency(wrong_colony);
        CHECK(fenced.status==PersistentWorld::SupervisorStatus::RescuePending);
        CHECK(fenced.command_id==route.command_id);
        CHECK(reopened.world().children_physically_aboard(source)==1);
        CHECK(reopened.pending_autonomous_family_rescue(route.command_id).has_value());
        bool crashed=false;
        try{(void)reopened.resume_next_pending_autonomous_rescue(
            FailPoint::AfterSnapshotWrite);}
        catch(const std::runtime_error&){crashed=true;}
        CHECK(crashed);
        CHECK(reopened.world().children_physically_aboard(source)==1);
        CHECK(reopened.pending_autonomous_family_rescue(route.command_id).has_value());
        CHECK(!reopened.verified_atomic_family_departure(route.command_id));
    }
    // Second restart: host calls supervisor only; supervisor recovers the
    // already-reserved command, never invents a second child or ID.
    {
        PersistentWorld reopened(db,Simulation(7992));
        const auto resumed=reopened.supervise_autonomous_colony_emergency(route);
        CHECK(resumed.status==PersistentWorld::SupervisorStatus::RescueCompleted);
        CHECK(resumed.receiver==near);
        CHECK(reopened.verified_atomic_family_departure(route.command_id));
        CHECK(!reopened.pending_autonomous_family_rescue(route.command_id));
        CHECK(!reopened.resume_next_pending_autonomous_rescue());
        const auto again=reopened.supervise_autonomous_colony_emergency(route);
        CHECK(again.status==PersistentWorld::SupervisorStatus::RescueCompleted);
        CHECK(reopened.world().colony_ship(source)->crew==0);
        CHECK(reopened.world().colony_ship(far)->crew==2);
        CHECK(reopened.world().colony_ship(near)->crew==4);
        CHECK(reopened.world().children_physically_aboard(source)==0);
        CHECK(reopened.world().children_physically_aboard(near)==1);
        CHECK(reopened.world().physical_dependents().at(child).guardian_id==1);
        CHECK(reopened.world().npc_records().size()==6);
        CHECK(reopened.world().total_material_kg()==original_material);
        namespace re=galaxy::regional_events;
        re::CheckpointStore region(db,region_scope);
        const auto checkpoint=region.load();
        CHECK(checkpoint.has_value());
        CHECK(checkpoint->revision==3);
        CHECK(checkpoint->scheduler.colony("source").population==0);
        CHECK(checkpoint->scheduler.colony("far").population==2);
        CHECK(checkpoint->scheduler.colony("near").population==5);
        const auto food=reopened.world().life_support(near)->food_kg;
        reopened.steps(700-reopened.world().tick());
        CHECK(reopened.world().life_support(near)->food_kg==food-15);
        CHECK(reopened.verified_atomic_family_departure(route.command_id));
    }
    {
        PersistentWorld restarted(db,Simulation(7993));
        CHECK(restarted.verified_atomic_family_departure(route.command_id));
        CHECK(restarted.world().physical_dependents().at(child).home_ship==near);
        CHECK(restarted.world().total_material_kg()==original_material);
    }
    PersistentWorld::recover_copy(db,recovered);
    {
        PersistentWorld copied(recovered,Simulation(7994));
        CHECK(copied.verified_atomic_family_departure(route.command_id));
        CHECK(!copied.resume_next_pending_autonomous_rescue());
    }
    PersistentWorld::recover_copy(db,tampered);
    {
        sqlite3* raw=nullptr;
        CHECK(sqlite3_open(tampered.c_str(),&raw)==SQLITE_OK);
        sql(raw,"DROP TRIGGER sg_colony_npc_supervisor_member_immutable_update");
        sql(raw,"UPDATE sg_colony_npc_supervisor_member SET guardian_id='2' "
                "WHERE command_id='7974' AND person_id='"+std::to_string(child)+"'");
        sql(raw,"CREATE TRIGGER sg_colony_npc_supervisor_member_immutable_update "
                "BEFORE UPDATE ON sg_colony_npc_supervisor_member "
                "BEGIN SELECT RAISE(ABORT,'immutable NPC supervisor'); END");
        sqlite3_close(raw);
        bool rejected=false;
        try{PersistentWorld corrupted(tampered,Simulation(7995));}
        catch(const std::runtime_error&){rejected=true;}
        CHECK(rejected);
    }
    clean(db);clean(recovered);clean(tampered);
    std::cout<<"PASS v79: automatic NPC warning delivery, no-safe-ship retry, "
        "same-commit plan, two process restarts, failpoint and tamper rejection\n";
    return 0;
}catch(const std::exception& e){
    std::cerr<<e.what()<<'\n';return 1;
}
}
