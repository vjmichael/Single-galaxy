// INT-02: original v77 TWO-HOUSEHOLD evacuation reintroduced against
// the current single SQLite/ART02/ECON03 AtomicColonyFamilyPlan authority.
// Never use the removed historic ExecutionPlan or invent child NPC workers.
#include "galaxy/Persistence.hpp"
#include "galaxy/NpcAppearanceGenome.hpp"
#include "galaxy/RegionalModeAuthority.hpp"
#include "galaxy/ColonySafety.hpp"
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace galaxy;
namespace re=galaxy::regional_events;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("INT02 two-households: ")+#x+" at "+std::to_string(__LINE__));}while(false)
static void clean(const std::string& file){
    std::error_code ec;
    for(auto suffix:{"","-wal","-shm"})std::filesystem::remove(file+suffix,ec);
}
int main(){
try {
    const auto db=(std::filesystem::temp_directory_path()/
        ("sg_int02_households_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".sqlite")).string();
    const auto recovered=db+".recovered";
    const std::string scope="int02-two-household-original",region_scope="int02-family-region";
    constexpr std::uint64_t seed=9211;
    const std::array<std::uint64_t,2> children{(1ULL<<62)+9211,(1ULL<<62)+9212};
    const std::array<std::uint64_t,2> guardians{1,3};
    EntityId source=0,destination=0;
    std::int64_t conserved_material=0;
    {
        Simulation genesis(seed);
        source=genesis.add_colony_ship(7,{0,0},100,4);
        destination=genesis.add_colony_ship(7,{0,0},100,2);
        genesis.provision_colony_genesis(source,100,100,100,200,500);
        genesis.provision_colony_genesis(destination,1000,1000,1000,1000,1000);
        genesis.enable_individual_workforce();
        genesis.upgrade_assignment_snapshot_format();
        genesis.enable_passenger_life_support();
        conserved_material=genesis.total_material_kg();
        PersistentWorld world(db,std::move(genesis));
        {
            NpcAppearanceStore family(db,scope,seed,
                [&](std::uint64_t id){return world.world().npc_records().count(id)!=0;});
            family.register_settlement(source,4);
            family.register_settlement(destination,2);
            for(std::uint64_t id=1;id<=4;++id)family.enroll_family_founder(id,source);
            for(std::uint64_t id=5;id<=6;++id)family.enroll_family_founder(id,destination);
        }
        re::CheckpointStore regional(db,region_scope);
        re::Scheduler schedule;
        cold_colony::State a{};a.population=4;
        cold_colony::State b{};b.population=2;
        schedule.add_colony("origin",a);
        schedule.add_colony("receiver",b);
        CHECK(regional.save(schedule,0)==1);
        re::RegionalModeAuthority mode(regional);
        CHECK(mode.enroll().generation==1);
        for(std::size_t i=0;i<children.size();++i){
            SocietyBirthPlan birth{};
            birth.expected_world_generation=world.generation();
            birth.expected_region_revision=1+i;
            birth.expected_owner_generation=1;
            birth.family_seed=seed;
            birth.expected_family_revision=i;
            birth.event_id=92101+i;
            birth.child_id=children[i];
            birth.parent_a=i==0?1:3;
            birth.parent_b=i==0?2:4;
            birth.home_ship=source;
            birth.family_scope=scope;
            birth.region_scope=region_scope;
            birth.regional_colony="origin";
            world.step_with_atomic_dependent_birth(birth);
        }
        CHECK(regional.load()->revision==3);
        CHECK(regional.load()->scheduler.colony("origin").population==6);
        for(std::size_t i=0;i<children.size();++i){
            Command registration{};
            registration.id=92201+i;
            registration.sequence=registration.id;
            registration.issuer=Simulation::kServerAuthority;
            registration.actor=source;
            registration.target=source;
            registration.action=Action::HostRegisterDependent;
            registration.transport_id=children[i];
            registration.passenger_ids={guardians[i]};
            registration.execute_tick=world.world().tick()+1;
            CHECK(world.submit_host_dependent_registration(registration,scope,seed));
            world.step_with_dependent_registration();
        }
        CHECK(world.world().children_physically_aboard(source)==2);
        CHECK(world.world().npc_records().size()==6);
        CHECK(world.world().total_material_kg()==conserved_material);
        world.steps(380-world.world().tick());
        CHECK(static_cast<int>(colony_safety::assess(world.world(),source,source).risk)>=
              static_cast<int>(colony_safety::Risk::Critical));
        PersistentWorld::AtomicColonyFamilyPlan plan{};
        plan.family.source=source;
        plan.family.receiver=destination;
        plan.family.intent_id=92301;
        plan.family.expected_world_generation=world.generation();
        plan.family.family_scope=scope;
        plan.family.family_seed=seed;
        plan.family.adult_ids={1,2,3,4};
        plan.family.children={{children[0],guardians[0]},
                              {children[1],guardians[1]}};
        plan.command_id=92302;
        plan.expected_region_revision=3;
        plan.expected_owner_generation=1;
        plan.expected_origin_family_revision=2;
        plan.expected_receiver_family_revision=0;
        plan.region_scope=region_scope;
        plan.origin_colony="origin";
        plan.receiver_colony="receiver";
        plan.member_event_ids={92401,92402,92403,92404,92405,92406};
        CHECK(world.audit_colony_family_departure(plan.family).ready_for_transaction);
        auto omit_second_family=plan.family;
        omit_second_family.adult_ids={1,2};
        omit_second_family.children.pop_back();
        CHECK(!world.audit_colony_family_departure(omit_second_family).ready_for_transaction);
        auto swap_guardian=plan.family;
        swap_guardian.children[1].guardian_id=1;
        CHECK(!world.audit_colony_family_departure(swap_guardian).ready_for_transaction);
        CHECK(world.prepare_host_colony_family_departure(plan.family));
        CHECK(world.submit_host_atomic_family_departure(plan));
        const auto accepted=world.generation();
        auto duplicate=plan;
        duplicate.member_event_ids[5]=duplicate.member_event_ids[4];
        bool refused=false;
        try{world.step_with_atomic_family_departure(duplicate);}
        catch(const std::exception&){refused=true;}
        CHECK(refused && world.generation()==accepted);
        refused=false;
        try{world.step_with_atomic_family_departure(plan,FailPoint::AfterSnapshotWrite);}
        catch(const std::exception&){refused=true;}
        CHECK(refused && world.generation()==accepted);
        CHECK(world.world().colony_ship(source)->crew==4);
        CHECK(world.world().children_physically_aboard(source)==2);
        CHECK(regional.load()->revision==3);
        world.step_with_atomic_family_departure(plan);
        CHECK(world.verified_atomic_family_departure(plan.command_id));
        CHECK(world.world().colony_ship(source)->crew==0);
        CHECK(world.world().colony_ship(destination)->crew==6);
        CHECK(world.world().children_physically_aboard(source)==0);
        CHECK(world.world().children_physically_aboard(destination)==2);
        CHECK(world.world().npc_records().size()==6);
        CHECK(world.world().total_material_kg()==conserved_material);
        for(std::size_t i=0;i<children.size();++i){
            const auto& child=world.world().physical_dependents().at(children[i]);
            CHECK(child.guardian_id==guardians[i]);
            CHECK(child.home_ship==destination && child.location==destination);
        }
        {
            NpcAppearanceStore family(db,scope,seed,
                [&](std::uint64_t id){return world.world().npc_records().count(id)!=0;});
            CHECK(family.family_census(source).current_population==0);
            CHECK(family.family_census(destination).current_population==8);
            for(std::uint64_t id=1;id<=6;++id)
                CHECK(family.current_residence(id)==destination);
            for(auto id:children)CHECK(family.current_residence(id)==destination);
        }
        CHECK(regional.load()->revision==4);
        CHECK(regional.load()->scheduler.colony("origin").population==0);
        CHECK(regional.load()->scheduler.colony("receiver").population==8);
        refused=false;
        try{world.step_with_atomic_family_departure(plan);}
        catch(const std::exception&){refused=true;}
        CHECK(refused);
    }
    {
        PersistentWorld original(db,Simulation(92303));
        CHECK(original.verified_atomic_family_departure(92302));
        CHECK(original.world().children_physically_aboard(destination)==2);
        CHECK(original.world().colony_ship(source)->crew==0);
    }
    PersistentWorld::recover_copy(db,recovered);
    {
        PersistentWorld survivor(recovered,Simulation(92304));
        CHECK(survivor.verified_atomic_family_departure(92302));
        CHECK(survivor.world().children_physically_aboard(destination)==2);
        CHECK(survivor.world().total_material_kg()==conserved_material);
        CHECK(re::CheckpointStore(recovered,region_scope).load()->scheduler.colony("receiver").population==8);
    }
    clean(db);clean(recovered);
    std::cout<<"INT02 PASS: two original households, six named adults, two ART02 physical children, atomic custody, rollback/replay and independent recovery\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
