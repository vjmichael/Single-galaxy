#include "galaxy/Persistence.hpp"
#include "galaxy/NpcAppearanceGenome.hpp"
#include "galaxy/RegionalModeAuthority.hpp"
#include "galaxy/ColonySafety.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace galaxy;
namespace re=galaxy::regional_events;
#define CHECK(c) do{if(!(c))throw std::runtime_error(std::string("AUTO04D4D3A: ")+ #c+" line "+std::to_string(__LINE__));}while(false)
template<class F>void denied(F f){bool reject=false;try{f();}catch(const std::exception&){reject=true;}CHECK(reject);}
static void tamper(const std::string& path,const char* sql){
    sqlite3* db=nullptr;CHECK(sqlite3_open_v2(path.c_str(),&db,SQLITE_OPEN_READWRITE,nullptr)==SQLITE_OK);
    char* err=nullptr;const auto rc=sqlite3_exec(db,sql,nullptr,nullptr,&err);
    if(rc!=SQLITE_OK){std::string s=err?err:"SQLite error";sqlite3_free(err);sqlite3_close(db);throw std::runtime_error(s);}
    sqlite3_close(db);
}
static void cleanup(const std::string& path){
    for(const auto& suffix:{"","-wal","-shm"})std::filesystem::remove(path+suffix);
}
int main(){
try{
    const std::string path=(std::filesystem::temp_directory_path()/
        ("sg_two_household_v77_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".db")).string();
    const std::string replay=path+".replay",corrupted=path+".corrupted";
    const std::string forged_guardian=path+".guardian",forged_adults=path+".adults";
    const std::string family_scope="family-shared-v77",region_scope="region-v77";
    constexpr std::uint64_t seed=7703;
    const auto child1=(1ULL<<62)+771,child2=(1ULL<<62)+772;
    EntityId source=0,receiver=0;std::uint64_t last_generation=0,predecessor_generation=0;
    std::int64_t original_material=0;
    {
        Simulation genesis(seed);
        source=genesis.add_colony_ship(7,{0,0},100,4);
        receiver=genesis.add_colony_ship(7,{0,0},100,2);
        genesis.provision_colony_genesis(source,100,100,100,200,500);
        genesis.provision_colony_genesis(receiver,1000,1000,1000,1000,1000);
        genesis.enable_individual_workforce();
        genesis.upgrade_assignment_snapshot_format();
        genesis.enable_passenger_life_support();
        genesis.enable_physical_dependent_accounting();
        original_material=genesis.total_material_kg();
        PersistentWorld w(path,std::move(genesis));
        NpcAppearanceStore family(path,family_scope,seed,[&](auto id){
            return w.world().npc_records().count(id)!=0;
        });
        family.register_settlement(source,4);
        family.register_settlement(receiver,2);
        for(std::uint64_t n=1;n<=4;++n)family.enroll_family_founder(n,source);
        for(std::uint64_t n=5;n<=6;++n)family.enroll_family_founder(n,receiver);
        re::CheckpointStore regional(path,region_scope);
        re::Scheduler schedule;
        cold_colony::State origin{};origin.population=4;
        cold_colony::State destination{};destination.population=2;
        schedule.add_colony("origin",origin);schedule.add_colony("receiver",destination);
        CHECK(regional.save(schedule,0)==1);
        re::RegionalModeAuthority authority(regional);
        CHECK(authority.enroll().generation==1);
        SocietyBirthPlan birth{};
        birth.expected_world_generation=w.generation();
        birth.expected_region_revision=1;
        birth.expected_owner_generation=1;
        birth.family_seed=seed;birth.expected_family_revision=0;
        birth.event_id=9101;birth.child_id=child1;
        birth.parent_a=1;birth.parent_b=2;birth.home_ship=source;
        birth.family_scope=family_scope;birth.region_scope=region_scope;
        birth.regional_colony="origin";
        w.step_with_atomic_dependent_birth(birth);
        CHECK(family.family_census(source).current_population==5);
        birth.expected_world_generation=w.generation();
        birth.expected_region_revision=2;
        birth.expected_family_revision=1;
        birth.event_id=9102;birth.child_id=child2;
        birth.parent_a=3;birth.parent_b=4;
        w.step_with_atomic_dependent_birth(birth);
        CHECK(family.family_census(source).current_population==6);
        CHECK(regional.load()->revision==3);
        CHECK(regional.load()->scheduler.colony("origin").population==6);
        CHECK(w.world().physical_dependents().size()==2);
        CHECK(w.world().npc_records().size()==6);
        w.steps(640-w.world().tick());
        CHECK(static_cast<int>(colony_safety::assess(w.world(),source,source).risk)>=
            static_cast<int>(colony_safety::Risk::Critical));
        colony_family::DeparturePlan manifest{};
        manifest.source=source;manifest.receiver=receiver;
        manifest.intent_id=7771;manifest.expected_world_generation=w.generation();
        manifest.family_scope=family_scope;manifest.family_seed=seed;
        manifest.adult_ids={1,2,3,4};manifest.children={{child1,1},{child2,3}};
        CHECK(w.audit_colony_family_departure(manifest).ready_for_transaction);
        CHECK(w.prepare_host_colony_family_departure(manifest));
        predecessor_generation=w.generation();
        colony_family::ExecutionPlan plan{};
        plan.prepared=manifest;plan.command_id=8001;
        plan.expected_region_revision=3;plan.expected_owner_generation=1;
        plan.expected_origin_family_revision=2;
        plan.expected_destination_family_revision=0;
        plan.region_scope=region_scope;plan.origin_colony="origin";plan.destination_colony="receiver";
        using H=colony_family::ExecutionPlan::Household;
        plan.households={
            H{1,2,8001,8002,{{child1,8003}}},
            H{3,4,8004,8005,{{child2,8006}}}
        };
        auto bad=plan;bad.households[1].children.clear();
        denied([&]{w.step_with_atomic_colony_family_departure(bad);});
        bad=plan;bad.households[1].children[0].event_id=8003;
        denied([&]{w.step_with_atomic_colony_family_departure(bad);});
        bad=plan;bad.households[1].children[0].child_id=child1;
        denied([&]{w.step_with_atomic_colony_family_departure(bad);});
        bad=plan;bad.households[1].guardian_id=1;
        denied([&]{w.step_with_atomic_colony_family_departure(bad);});
        bad=plan;bad.households[1].co_parent_id=2;
        denied([&]{w.step_with_atomic_colony_family_departure(bad);});
        bad=plan;bad.households[1].guardian_event_id=8001;
        denied([&]{w.step_with_atomic_colony_family_departure(bad);});
        bad=plan;bad.co_parent_event_id=9999; // mixed legacy+v77 manifest forbidden
        denied([&]{w.step_with_atomic_colony_family_departure(bad);});
        denied([&]{w.step_with_atomic_colony_family_departure(
            plan,FailPoint::AfterSnapshotWrite);});
        CHECK(w.generation()==predecessor_generation);
        CHECK(w.world().colony_ship(source)->crew==4);
        CHECK(w.world().physical_dependents().at(child1).location==source);
        CHECK(w.world().physical_dependents().at(child2).location==source);
        CHECK(family.current_residence(child1)==source);
        CHECK(family.current_residence(child2)==source);
        CHECK(family.family_census(source).current_population==6);
        CHECK(regional.load()->revision==3);
        CHECK(w.world().colony_departures().empty());
        w.step_with_atomic_colony_family_departure(plan);
        CHECK(w.world().colony_ship(source)->crew==0);
        CHECK(w.world().colony_ship(receiver)->crew==6);
        CHECK(w.world().people_physically_aboard(receiver)==6);
        CHECK(w.world().dependents_physically_at(receiver)==2);
        CHECK(w.world().dependents_physically_at(source)==0);
        CHECK(w.world().npc_records().size()==6);
        CHECK(w.world().colony_departures().at(source).relocated_npcs==manifest.adult_ids);
        CHECK(w.world().colony_departures().at(source).relocated_children==
              std::vector<std::uint64_t>({child1,child2}));
        for(auto id:manifest.adult_ids)CHECK(family.current_residence(id)==receiver);
        CHECK(family.current_residence(child1)==receiver);
        CHECK(family.current_residence(child2)==receiver);
        CHECK(family.family_census(source).current_population==0);
        CHECK(family.family_census(receiver).current_population==8);
        CHECK(regional.load()->revision==4);
        CHECK(regional.load()->scheduler.colony("origin").population==0);
        CHECK(regional.load()->scheduler.colony("receiver").population==8);
        CHECK(w.world().total_material_kg()==original_material);
        CHECK(Simulation::from_snapshot(w.world().canonical_state()).canonical_state()==
            w.world().canonical_state());
        const auto receiver_food=w.world().life_support(receiver)->food_kg;
        w.steps(660-w.world().tick());
        CHECK(w.world().life_support(receiver)->food_kg==receiver_food-8);
        CHECK(w.world().total_material_kg()==original_material);
        last_generation=w.generation();
        CHECK(last_generation>predecessor_generation+3);
        denied([&]{w.step_with_atomic_colony_family_departure(plan);});
    }
    {
        PersistentWorld reopened(path,Simulation(3));
        CHECK(reopened.generation()==last_generation);
        CHECK(reopened.world().colony_ship(source)->crew==0);
        CHECK(reopened.world().physical_dependents().at(child1).location==receiver);
        CHECK(reopened.world().physical_dependents().at(child2).location==receiver);
    }
    PersistentWorld::recover_copy(path,replay);
    {
        PersistentWorld recovered(replay,Simulation(4));
        CHECK(recovered.generation()==last_generation);
        CHECK(recovered.world().dependents_physically_at(receiver)==2);
        re::CheckpointStore regional(replay,region_scope);
        CHECK(regional.load()->revision==4);
        CHECK(regional.load()->scheduler.colony("receiver").population==8);
    }
    PersistentWorld::recover_copy(path,corrupted);
    // Remove an ART02-linked individual receipt despite successful world
    // movement. Restore must refuse the now-incomplete 4+2 headcount.
    tamper(corrupted,("DROP TRIGGER sg_colony_family_member_no_delete; "
        "DELETE FROM sg_colony_family_household_member WHERE npc_id='"+
        std::to_string(child2)+"';").c_str());
    denied([&]{PersistentWorld invalid(corrupted,Simulation(5));});
    // v78: an offline edit of the original prepared child->guardian map
    // must be rejected even though the physical world, headcounts and all
    // executed member receipts have not changed.
    PersistentWorld::recover_copy(path,forged_guardian);
    tamper(forged_guardian,("DROP TRIGGER sg_colony_family_prepare_no_update; "
        "UPDATE sg_colony_family_departure_prepare SET child_guardians='"+
        std::to_string(child1)+":1,"+std::to_string(child2)+
        ":1' WHERE intent_id='7771';").c_str());
    denied([&]{PersistentWorld invalid(forged_guardian,Simulation(6));});
    // The immutable prepared four-adult roster is independent proof too.
    PersistentWorld::recover_copy(path,forged_adults);
    tamper(forged_adults,"DROP TRIGGER sg_colony_family_prepare_no_update; "
        "UPDATE sg_colony_family_departure_prepare SET adult_roster='1,2,3,6' "
        "WHERE intent_id='7771';");
    denied([&]{PersistentWorld invalid(forged_adults,Simulation(7));});
    cleanup(path);cleanup(replay);cleanup(corrupted);
    cleanup(forged_guardian);cleanup(forged_adults);
    std::cout<<"PASS v77: two ART02 households, one world departure, atomic cold-region census, restart/replay and corruption refusal\n";
    return 0;
}catch(const std::exception& e){
    std::cerr<<"FAIL v77 two-household evacuation: "<<e.what()<<'\n';
    return 1;
}
}
