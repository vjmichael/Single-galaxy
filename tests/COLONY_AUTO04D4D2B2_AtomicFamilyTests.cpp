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
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("AUTO04D2B2: ")+ #x+" at "+std::to_string(__LINE__));}while(false)
static void exec(sqlite3* db,const std::string& sql){
    char* error=nullptr;
    if(sqlite3_exec(db,sql.c_str(),nullptr,nullptr,&error)!=SQLITE_OK){
        const std::string why=error?error:"unknown";sqlite3_free(error);
        throw std::runtime_error("tamper fixture: "+why);
    }
}
static void clean(const std::string& path){
    for(const auto& suffix:{"","-wal","-shm"})
        std::filesystem::remove(path+suffix);
}
int main(){
try {
    const auto db=(std::filesystem::temp_directory_path()/
        ("sg_family_v76_"+std::to_string(std::chrono::steady_clock::now()
            .time_since_epoch().count())+".sqlite")).string();
    const auto clone=db+".recovered",tampered=db+".tamper";
    const auto child=(1ULL<<62)+7601;
    constexpr std::uint64_t family_seed=7691;
    const std::string family_scope="v76-atomic-family",region_scope="reg-76";
    EntityId home=0,receiver=0;
    std::int64_t material=0;
    std::uint64_t accepted_generation=0,completed_generation=0;
    {
        Simulation genesis(7601);
        home=genesis.add_colony_ship(7,{0,0},100,2);
        receiver=genesis.add_colony_ship(7,{0,0},100,2);
        genesis.provision_colony_genesis(home,100,100,100,200,500);
        genesis.provision_colony_genesis(receiver,1000,1000,1000,1000,1000);
        genesis.enable_individual_workforce();
        genesis.upgrade_assignment_snapshot_format();
        genesis.enable_passenger_life_support();
        material=genesis.total_material_kg();
        PersistentWorld world(db,std::move(genesis));
        {
            NpcAppearanceStore family(db,family_scope,family_seed,
                [&](auto id){return world.world().npc_records().count(id)!=0;});
            family.register_settlement(home,2);
            family.register_settlement(receiver,2);
            for(std::uint64_t id=1;id<=2;++id)family.enroll_family_founder(id,home);
            for(std::uint64_t id=3;id<=4;++id)family.enroll_family_founder(id,receiver);
        }
        namespace re=galaxy::regional_events;
        re::CheckpointStore regional(db,region_scope);
        re::Scheduler scheduler;
        cold_colony::State normal{};normal.population=2;
        scheduler.add_colony("source",normal);
        scheduler.add_colony("destination",normal);
        CHECK(regional.save(scheduler,0)==1);
        re::RegionalModeAuthority authority(regional);
        CHECK(authority.enroll().generation==1);
        SocietyBirthPlan birth{};
        birth.expected_world_generation=world.generation();
        birth.expected_region_revision=1;
        birth.expected_owner_generation=1;
        birth.family_seed=family_seed;
        birth.expected_family_revision=0;
        birth.event_id=7601;birth.child_id=child;
        birth.parent_a=1;birth.parent_b=2;
        birth.home_ship=home;
        birth.family_scope=family_scope;
        birth.region_scope=region_scope;
        birth.regional_colony="source";
        world.step_with_atomic_dependent_birth(birth);
        Command registration{};
        registration.id=7602;registration.sequence=7602;
        registration.issuer=Simulation::kServerAuthority;
        registration.actor=home;registration.target=home;
        registration.action=Action::HostRegisterDependent;
        registration.transport_id=child;registration.passenger_ids={1};
        registration.execute_tick=world.world().tick()+1;
        CHECK(world.submit_host_dependent_registration(registration,family_scope,family_seed));
        world.step_with_dependent_registration();
        CHECK(world.world().children_physically_aboard(home)==1);
        CHECK(world.world().npc_records().size()==4);
        CHECK(world.world().total_material_kg()==material);
        world.steps(640-world.world().tick());
        CHECK(colony_safety::assess(world.world(),home,home).risk==colony_safety::Risk::Critical);
        PersistentWorld::AtomicColonyFamilyPlan plan{};
        plan.family.source=home;plan.family.receiver=receiver;
        plan.family.intent_id=7603;plan.family.expected_world_generation=world.generation();
        plan.family.family_scope=family_scope;plan.family.family_seed=family_seed;
        plan.family.adult_ids={1,2};plan.family.children={{child,1}};
        plan.command_id=7604;
        plan.expected_region_revision=2;
        plan.expected_owner_generation=1;
        plan.expected_origin_family_revision=1;
        plan.expected_receiver_family_revision=0;
        plan.region_scope=region_scope;
        plan.origin_colony="source";plan.receiver_colony="destination";
        plan.member_event_ids={7701,7702,7703};
        auto broken=plan.family;broken.children.clear();
        CHECK(!world.prepare_host_colony_family_departure(broken));
        CHECK(world.prepare_host_colony_family_departure(plan.family));
        CHECK(world.submit_host_atomic_family_departure(plan));
        accepted_generation=world.generation();
        CHECK(!world.verified_atomic_family_departure(plan.command_id));
        bool generic_blocked=false;
        try{world.step();}catch(const std::runtime_error&){generic_blocked=true;}
        CHECK(generic_blocked);
        CHECK(world.generation()==accepted_generation);
        auto stale_region=plan;stale_region.expected_region_revision=1;
        bool region_blocked=false;
        try{world.step_with_atomic_family_departure(stale_region);}
        catch(const std::exception&){region_blocked=true;}
        CHECK(region_blocked);
        CHECK(world.generation()==accepted_generation);
        bool crash=false;
        try{world.step_with_atomic_family_departure(plan,FailPoint::AfterSnapshotWrite);}
        catch(const std::runtime_error&){crash=true;}
        CHECK(crash);
        CHECK(world.generation()==accepted_generation);
        CHECK(world.world().children_physically_aboard(home)==1);
        CHECK(world.world().colony_ship(home)->crew==2);
        CHECK(world.world().colony_ship(receiver)->crew==2);
        {
            NpcAppearanceStore family(db,family_scope,family_seed,
                [&](auto id){return world.world().npc_records().count(id)!=0;});
            CHECK(family.current_residence(1)==home);
            CHECK(family.current_residence(2)==home);
            CHECK(family.current_residence(child)==home);
            CHECK(family.family_census(home).current_population==3);
            CHECK(family.family_census(receiver).current_population==2);
        }
        const auto regional_before=regional.load();
        CHECK(regional_before.has_value());
        CHECK(regional_before->revision==2);
        CHECK(regional_before->scheduler.colony("source").population==3);
        CHECK(regional_before->scheduler.colony("destination").population==2);
        world.step_with_atomic_family_departure(plan);
        completed_generation=world.generation();
        CHECK(completed_generation==accepted_generation+1);
        CHECK(world.verified_atomic_family_departure(plan.command_id));
        CHECK(world.world().colony_ship(home)->crew==0);
        CHECK(world.world().colony_ship(receiver)->crew==4);
        CHECK(world.world().children_physically_aboard(home)==0);
        CHECK(world.world().children_physically_aboard(receiver)==1);
        CHECK(world.world().people_physically_aboard(receiver)==4);
        CHECK(world.world().physical_dependents().at(child).guardian_id==1);
        CHECK(world.world().physical_dependents().at(child).home_ship==receiver);
        CHECK(world.world().npc_records().size()==4);
        CHECK(world.world().total_material_kg()==material);
        {
            NpcAppearanceStore family(db,family_scope,family_seed,
                [&](auto id){return world.world().npc_records().count(id)!=0;});
            CHECK(family.current_residence(1)==receiver);
            CHECK(family.current_residence(2)==receiver);
            CHECK(family.current_residence(child)==receiver);
            CHECK(family.family_census(home).current_population==0);
            CHECK(family.family_census(receiver).current_population==5);
            CHECK(family.family_census(home).revision==4);
            CHECK(family.family_census(receiver).revision==3);
        }
        const auto result=regional.load();
        CHECK(result.has_value());
        CHECK(result->revision==3);
        CHECK(result->scheduler.colony("source").population==0);
        CHECK(result->scheduler.colony("destination").population==5);
        const auto food=world.world().life_support(receiver)->food_kg;
        world.steps(660-world.world().tick());
        CHECK(world.world().life_support(receiver)->food_kg==food-5);
        CHECK(world.world().total_material_kg()==material);
    }
    {
        PersistentWorld restarted(db,Simulation(7700));
        CHECK(restarted.world().children_physically_aboard(receiver)==1);
        CHECK(restarted.world().colony_ship(home)->crew==0);
        CHECK(restarted.verified_atomic_family_departure(7604));
        CHECK(restarted.world().total_material_kg()==material);
    }
    PersistentWorld::recover_copy(db,clone);
    {
        PersistentWorld replay(clone,Simulation(7701));
        CHECK(replay.world().children_physically_aboard(receiver)==1);
        CHECK(replay.world().physical_dependents().at(child).guardian_id==1);
        CHECK(replay.verified_atomic_family_departure(7604));
        CHECK(replay.world().total_material_kg()==material);
    }
    PersistentWorld::recover_copy(db,tampered);
    {
        sqlite3* raw=nullptr;
        CHECK(sqlite3_open(tampered.c_str(),&raw)==SQLITE_OK);
        exec(raw,"DROP TRIGGER sg_colony_family_evacuation_member_immutable_delete");
        exec(raw,"DELETE FROM sg_colony_family_evacuation_member "
                 "WHERE command_id='7604' AND person_id='"+std::to_string(child)+"'");
        sqlite3_close(raw);
        bool refused=false;
        try{PersistentWorld corrupted(tampered,Simulation(7702));}
        catch(const std::runtime_error&){refused=true;}
        CHECK(refused);
    }
    clean(db);clean(clone);clean(tampered);
    std::cout<<"PASS v76: one world/ART02/ECON03 family evacuation, rollback, recovery, tamper refusal\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
