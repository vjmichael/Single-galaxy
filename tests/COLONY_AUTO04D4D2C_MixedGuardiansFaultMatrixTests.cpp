#include "galaxy/Persistence.hpp"
#include "galaxy/NpcAppearanceGenome.hpp"
#include "galaxy/RegionalModeAuthority.hpp"
#include "galaxy/ColonySafety.hpp"
#include <sqlite3.h>
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("AUTO04D2C: ")+ #x+" at "+std::to_string(__LINE__));}while(false)
namespace re=galaxy::regional_events;
static void sql(sqlite3* db,const std::string& statement){
    char* error=nullptr;
    if(sqlite3_exec(db,statement.c_str(),nullptr,nullptr,&error)!=SQLITE_OK){
        const std::string reason=error?error:"unknown";sqlite3_free(error);
        throw std::runtime_error("fixture SQL: "+reason);
    }
}
static void clean(const std::string& path){
    for(const auto* suffix:{"","-wal","-shm"})
        std::filesystem::remove(path+suffix);
}
int main(){
try{
    const auto db=(std::filesystem::temp_directory_path()/
        ("sg_family_v77_"+std::to_string(std::chrono::steady_clock::now()
            .time_since_epoch().count())+".sqlite")).string();
    const auto recovered=db+".recovered";
    constexpr std::uint64_t seed=7791;
    const std::string scope="v77-mixed-guardians",region_scope="reg-77";
    const std::array<std::uint64_t,3> children={
        (1ULL<<62)+7701,(1ULL<<62)+7702,(1ULL<<62)+7703};
    const std::array<std::uint64_t,3> guardians={1,2,1};
    EntityId source=0,destination=0;
    std::int64_t original_material=0;
    {
        Simulation genesis(7711);
        source=genesis.add_colony_ship(7,{0,0},100,2);
        destination=genesis.add_colony_ship(7,{0,0},100,2);
        genesis.provision_colony_genesis(source,100,100,100,200,500);
        genesis.provision_colony_genesis(destination,1000,1000,1000,1000,1000);
        genesis.enable_individual_workforce();
        genesis.upgrade_assignment_snapshot_format();
        genesis.enable_passenger_life_support();
        original_material=genesis.total_material_kg();
        PersistentWorld world(db,std::move(genesis));
        {
            NpcAppearanceStore family(db,scope,seed,
                [&](std::uint64_t id){return world.world().npc_records().count(id)!=0;});
            family.register_settlement(source,2);
            family.register_settlement(destination,2);
            family.enroll_family_founder(1,source);
            family.enroll_family_founder(2,source);
            family.enroll_family_founder(3,destination);
            family.enroll_family_founder(4,destination);
        }
        re::CheckpointStore region(db,region_scope);
        re::Scheduler scheduler;
        cold_colony::State populated{};populated.population=2;
        scheduler.add_colony("source",populated);
        scheduler.add_colony("destination",populated);
        CHECK(region.save(scheduler,0)==1);
        re::RegionalModeAuthority mode(region);
        CHECK(mode.enroll().generation==1);
        for(std::size_t i=0;i<children.size();++i){
            SocietyBirthPlan birth{};
            birth.expected_world_generation=world.generation();
            birth.expected_region_revision=1+i;
            birth.expected_owner_generation=1;
            birth.family_seed=seed;
            birth.expected_family_revision=i;
            birth.event_id=7701+i;
            birth.child_id=children[i];
            birth.parent_a=1;birth.parent_b=2;
            birth.home_ship=source;birth.family_scope=scope;
            birth.region_scope=region_scope;
            birth.regional_colony="source";
            world.step_with_atomic_dependent_birth(birth);
        }
        const auto regional_after_births=region.load();
        CHECK(regional_after_births.has_value());
        CHECK(regional_after_births->revision==4);
        CHECK(regional_after_births->scheduler.colony("source").population==5);
        CHECK(regional_after_births->scheduler.colony("destination").population==2);
        for(std::size_t i=0;i<children.size();++i){
            Command registration{};
            registration.id=7801+i;
            registration.sequence=registration.id;
            registration.issuer=Simulation::kServerAuthority;
            registration.actor=source;registration.target=source;
            registration.action=Action::HostRegisterDependent;
            registration.transport_id=children[i];
            registration.passenger_ids={guardians[i]};
            registration.execute_tick=world.world().tick()+1;
            CHECK(world.submit_host_dependent_registration(registration,scope,seed));
            world.step_with_dependent_registration();
        }
        CHECK(world.world().npc_records().size()==4);
        CHECK(world.world().children_physically_aboard(source)==3);
        CHECK(world.world().total_material_kg()==original_material);
        world.steps(360-world.world().tick());
        CHECK(colony_safety::assess(world.world(),source,source).risk==
              colony_safety::Risk::Critical);
        PersistentWorld::AtomicColonyFamilyPlan plan{};
        plan.family.source=source;
        plan.family.receiver=destination;
        plan.family.intent_id=7901;
        plan.family.expected_world_generation=world.generation();
        plan.family.family_scope=scope;plan.family.family_seed=seed;
        plan.family.adult_ids={1,2};
        for(std::size_t i=0;i<children.size();++i)
            plan.family.children.push_back({children[i],guardians[i]});
        plan.command_id=7902;
        plan.expected_region_revision=4;
        plan.expected_owner_generation=1;
        plan.expected_origin_family_revision=3;
        plan.expected_receiver_family_revision=0;
        plan.region_scope=region_scope;
        plan.origin_colony="source";plan.receiver_colony="destination";
        plan.member_event_ids={7911,7912,7913,7914,7915};
        auto incomplete=plan.family;
        incomplete.children.pop_back();
        CHECK(!world.prepare_host_colony_family_departure(incomplete));
        auto wrong_guardian=plan.family;
        wrong_guardian.children[1].guardian_id=1;
        CHECK(!world.prepare_host_colony_family_departure(wrong_guardian));
        auto duplicate=plan;
        duplicate.member_event_ids[3]=duplicate.member_event_ids[2];
        CHECK(world.prepare_host_colony_family_departure(plan.family));
        CHECK(world.submit_host_atomic_family_departure(plan));
        const auto accepted=world.generation();
        bool duplicate_rejected=false;
        try{world.step_with_atomic_family_departure(duplicate);}
        catch(const std::exception&){duplicate_rejected=true;}
        CHECK(duplicate_rejected);
        CHECK(world.generation()==accepted);
        const auto boundary=mode.state();
        const auto cp=region.load();
        CHECK(cp.has_value());
        CHECK(boundary.checkpoint_revision==cp->revision);
        CHECK(boundary.boundary_tick==cp->scheduler.tick());
        auto preparing=mode.prepare("v77-interrupted-handoff",re::SimulationOwner::Detailed,
            boundary.generation,boundary.checkpoint_revision,boundary.boundary_tick);
        CHECK(preparing.phase==re::TransitionPhase::ToDetailed);
        bool handoff_refused=false;
        try{world.step_with_atomic_family_departure(plan);}
        catch(const std::exception&){handoff_refused=true;}
        CHECK(handoff_refused);
        CHECK(world.generation()==accepted);
        CHECK(world.world().children_physically_aboard(source)==3);
        CHECK(world.world().colony_ship(source)->crew==2);
        CHECK(world.world().colony_ship(destination)->crew==2);
        const auto rollback=mode.abort("v77-interrupted-handoff",boundary.generation);
        CHECK(rollback.generation==boundary.generation+1);
        CHECK(rollback.phase==re::TransitionPhase::Stable);
        bool stale_owner=false;
        try{world.step_with_atomic_family_departure(plan);}
        catch(const std::exception&){stale_owner=true;}
        CHECK(stale_owner);
        CHECK(world.generation()==accepted);
        plan.expected_owner_generation=rollback.generation;
        bool crash_refused=false;
        try{world.step_with_atomic_family_departure(plan,FailPoint::AfterSnapshotWrite);}
        catch(const std::exception&){crash_refused=true;}
        CHECK(crash_refused);
        CHECK(world.generation()==accepted);
        CHECK(world.world().children_physically_aboard(source)==3);
        {
            NpcAppearanceStore family(db,scope,seed,
                [&](std::uint64_t id){return world.world().npc_records().count(id)!=0;});
            CHECK(family.family_census(source).current_population==5);
            CHECK(family.family_census(destination).current_population==2);
            CHECK(family.family_census(source).revision==3);
            for(const auto id:children)CHECK(family.current_residence(id)==source);
        }
        CHECK(region.load()->revision==4);
        CHECK(region.load()->scheduler.colony("source").population==5);
        CHECK(region.load()->scheduler.colony("destination").population==2);
        world.step_with_atomic_family_departure(plan);
        CHECK(world.generation()==accepted+1);
        CHECK(world.verified_atomic_family_departure(7902));
        CHECK(world.world().colony_ship(source)->crew==0);
        CHECK(world.world().colony_ship(destination)->crew==4);
        CHECK(world.world().people_physically_aboard(destination)==4);
        CHECK(world.world().children_physically_aboard(source)==0);
        CHECK(world.world().children_physically_aboard(destination)==3);
        CHECK(world.world().npc_records().size()==4);
        CHECK(world.world().total_material_kg()==original_material);
        for(std::size_t i=0;i<children.size();++i){
            const auto& child=world.world().physical_dependents().at(children[i]);
            CHECK(child.guardian_id==guardians[i]);
            CHECK(child.home_ship==destination);
            CHECK(child.location==destination);
        }
        {
            NpcAppearanceStore family(db,scope,seed,
                [&](std::uint64_t id){return world.world().npc_records().count(id)!=0;});
            CHECK(family.family_census(source).current_population==0);
            CHECK(family.family_census(destination).current_population==7);
            CHECK(family.family_census(source).revision==8);
            CHECK(family.family_census(destination).revision==5);
            for(const auto id:children)CHECK(family.current_residence(id)==destination);
        }
        CHECK(region.load()->revision==5);
        CHECK(region.load()->scheduler.colony("source").population==0);
        CHECK(region.load()->scheduler.colony("destination").population==7);
        const auto food=world.world().life_support(destination)->food_kg;
        world.steps(380-world.world().tick());
        CHECK(world.world().life_support(destination)->food_kg==food-7);
        CHECK(world.verified_atomic_family_departure(7902));
    }
    {
        PersistentWorld restarted(db,Simulation(7798));
        CHECK(restarted.world().children_physically_aboard(destination)==3);
        CHECK(restarted.world().people_physically_aboard(destination)==4);
        CHECK(restarted.verified_atomic_family_departure(7902));
    }
    PersistentWorld::recover_copy(db,recovered);
    {
        PersistentWorld surviving(recovered,Simulation(7799));
        CHECK(surviving.world().children_physically_aboard(destination)==3);
        CHECK(surviving.verified_atomic_family_departure(7902));
        sqlite3* raw=nullptr;
        CHECK(sqlite3_open(recovered.c_str(),&raw)==SQLITE_OK);
        sql(raw,"DROP TRIGGER sg_colony_family_evacuation_member_immutable_delete");
        sql(raw,"DELETE FROM sg_colony_family_evacuation_member "
                "WHERE command_id='7902' AND person_id='"+std::to_string(children[1])+"'");
        sqlite3_close(raw);
        // A live query MUST re-audit the DB: do not trust the cached world
        // or a preserved evacuation receipt header with a missing child row.
        bool live_refused=false;
        try{(void)surviving.verified_atomic_family_departure(7902);}
        catch(const std::exception&){live_refused=true;}
        CHECK(live_refused);
    }
    bool restart_refused=false;
    try{PersistentWorld forged(recovered,Simulation(7800));}
    catch(const std::exception&){restart_refused=true;}
    CHECK(restart_refused);
    clean(db);clean(recovered);
    std::cout<<"PASS v77: 3 physical children, 2 guardians, frozen owner, failpoint, full receipt re-audit\n";
    return 0;
}catch(const std::exception& e){
    std::cerr<<e.what()<<'\n';return 1;
}
}
