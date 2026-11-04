#include "galaxy/Persistence.hpp"
#include "galaxy/NpcAppearanceGenome.hpp"
#include "galaxy/RegionalModeAuthority.hpp"
#include "galaxy/MedicalSocietyBridge.hpp"
#include <sqlite3.h>
#include <filesystem>
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
namespace re=galaxy::regional_events;
#define CHECK(c) do{if(!(c))throw std::runtime_error(std::string("AUTO04D4D2 family: ")+ #c+" line "+std::to_string(__LINE__));}while(false)
static std::uint64_t retained_snapshot_count(const std::string& path,std::uint64_t gen){
    sqlite3* db=nullptr;
    CHECK(sqlite3_open_v2(path.c_str(),&db,SQLITE_OPEN_READONLY,nullptr)==SQLITE_OK);
    sqlite3_stmt* query=nullptr;
    CHECK(sqlite3_prepare_v2(db,"SELECT COUNT(*) FROM snapshots WHERE generation=?",
                             -1,&query,nullptr)==SQLITE_OK);
    CHECK(sqlite3_bind_int64(query,1,static_cast<sqlite3_int64>(gen))==SQLITE_OK);
    CHECK(sqlite3_step(query)==SQLITE_ROW);
    const auto count=static_cast<std::uint64_t>(sqlite3_column_int64(query,0));
    sqlite3_finalize(query);sqlite3_close(db);
    return count;
}
static void delete_historical_snapshot(const std::string& path,std::uint64_t gen){
    sqlite3* db=nullptr;
    CHECK(sqlite3_open_v2(path.c_str(),&db,SQLITE_OPEN_READWRITE,nullptr)==SQLITE_OK);
    sqlite3_stmt* query=nullptr;
    CHECK(sqlite3_prepare_v2(db,"DELETE FROM snapshots WHERE generation=?",
                             -1,&query,nullptr)==SQLITE_OK);
    CHECK(sqlite3_bind_int64(query,1,static_cast<sqlite3_int64>(gen))==SQLITE_OK);
    CHECK(sqlite3_step(query)==SQLITE_DONE);
    CHECK(sqlite3_changes(db)==1);
    sqlite3_finalize(query);sqlite3_close(db);
}
template<class F>static void denied(F fn){bool rejected=false;try{fn();}catch(const std::exception& e){std::cerr<<"Expected denial: "<<e.what()<<'\n';rejected=true;}CHECK(rejected);}
int main(){
try{
    const auto file=(std::filesystem::temp_directory_path()/
        ("sg_family_atomic_v59_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".db")).string();
    const auto copy=file+".replay";
    std::uint64_t source=0,receiver=0,child=(1ULL<<62)+740;
    std::int64_t material=0;std::uint64_t last_gen=0,predecessor_gen=0;
    const std::string scope="colony-family-v74",region_scope="colony-family-region-v74";
    constexpr std::uint64_t seed=7404;
    {
        Simulation genesis(seed);
        source=genesis.add_colony_ship(7,{0,0},100,2);
        receiver=genesis.add_colony_ship(7,{0,0},100,2);
        genesis.provision_colony_genesis(source,100,100,100,200,500);
        genesis.provision_colony_genesis(receiver,1000,1000,1000,1000,1000);
        genesis.enable_individual_workforce();
        genesis.upgrade_assignment_snapshot_format();
        genesis.enable_passenger_life_support();
        genesis.enable_physical_dependent_accounting();
        material=genesis.total_material_kg();
        PersistentWorld w(file,std::move(genesis));
        NpcAppearanceStore family(file,scope,seed,[&](auto id){
            return w.world().npc_records().count(id)!=0;
        });
        family.register_settlement(source,2);
        family.register_settlement(receiver,2);
        family.enroll_family_founder(1,source);
        family.enroll_family_founder(2,source);
        family.enroll_family_founder(3,receiver);
        family.enroll_family_founder(4,receiver);
        re::CheckpointStore region(file,region_scope);
        re::Scheduler schedule;
        cold_colony::State original{};original.population=2;
        cold_colony::State destination{};destination.population=2;
        schedule.add_colony("origin",original);
        schedule.add_colony("receiver",destination);
        CHECK(region.save(schedule,0)==1);
        re::RegionalModeAuthority authority(region);
        CHECK(authority.enroll().generation==1);
        SocietyBirthPlan birth{};
        birth.expected_world_generation=w.generation();
        birth.expected_region_revision=1;
        birth.expected_owner_generation=1;
        birth.family_seed=seed;
        birth.expected_family_revision=0;
        birth.event_id=9001;birth.child_id=child;
        birth.parent_a=1;birth.parent_b=2;
        birth.home_ship=source;birth.family_scope=scope;
        birth.region_scope=region_scope;birth.regional_colony="origin";
        w.step_with_atomic_dependent_birth(birth);
        CHECK(w.world().physical_dependents().at(child).location==source);
        CHECK(w.world().physical_dependents().at(child).birth_command==birth.event_id);
        CHECK(w.world().npc_records().size()==4);
        CHECK(w.world().life_support(source)->food_kg==100);
        CHECK(family.family_census(source).current_population==3);
        CHECK(region.load()->scheduler.colony("origin").population==3);
        CHECK(region.load()->revision==2);
        w.steps(640-w.world().tick());
        CHECK(static_cast<int>(colony_safety::assess(w.world(),source,source).risk)>=
              static_cast<int>(colony_safety::Risk::Critical));
        CHECK(w.world().life_support(source)->food_kg==4);
        colony_family::DeparturePlan manifest{};
        manifest.source=source;manifest.receiver=receiver;
        manifest.intent_id=5001;manifest.expected_world_generation=w.generation();
        manifest.family_scope=scope;manifest.family_seed=seed;
        manifest.adult_ids={1,2};
        manifest.children={{child,1}};
        const auto proof=w.audit_colony_family_departure(manifest);
        CHECK(proof.ready_for_transaction);
        CHECK(w.prepare_host_colony_family_departure(manifest));
        predecessor_gen=w.generation();
        colony_family::ExecutionPlan execution{};
        execution.prepared=manifest;execution.command_id=8001;
        execution.expected_region_revision=2;
        execution.expected_owner_generation=1;
        execution.expected_origin_family_revision=1;
        execution.expected_destination_family_revision=0;
        execution.co_parent_event_id=8002;
        execution.child_move_events={8003};
        execution.region_scope=region_scope;
        execution.origin_colony="origin";
        execution.destination_colony="receiver";
        auto wrong=execution;wrong.child_move_events={8002};
        denied([&]{w.step_with_atomic_colony_family_departure(wrong);});
        CHECK(w.world().colony_ship(source)->crew==2);
        CHECK(w.world().physical_dependents().at(child).location==source);
        denied([&]{w.step_with_atomic_colony_family_departure(execution,FailPoint::AfterSnapshotWrite);});
        CHECK(w.world().colony_ship(source)->crew==2);
        CHECK(w.world().physical_dependents().at(child).location==source);
        CHECK(family.current_residence(child)==source);
        CHECK(region.load()->revision==2);
        w.step_with_atomic_colony_family_departure(execution);
        CHECK(w.world().colony_ship(source)->crew==0);
        CHECK(w.world().colony_ship(receiver)->crew==4);
        CHECK(w.world().people_physically_aboard(receiver)==4);
        CHECK(w.world().dependents_physically_at(receiver)==1);
        CHECK(w.world().dependents_physically_at(source)==0);
        CHECK(w.world().physical_dependents().at(child).home_ship==receiver);
        CHECK(w.world().colony_departures().at(source).relocated_children==
              std::vector<std::uint64_t>({child}));
        CHECK(w.world().npc_records().size()==4);
        CHECK(family.current_residence(1)==receiver);
        CHECK(family.current_residence(2)==receiver);
        CHECK(family.current_residence(child)==receiver);
        CHECK(family.family_census(source).current_population==0);
        CHECK(family.family_census(receiver).current_population==5);
        CHECK(region.load()->revision==3);
        CHECK(region.load()->scheduler.colony("origin").population==0);
        CHECK(region.load()->scheduler.colony("receiver").population==5);
        CHECK(w.world().total_material_kg()==material);
        CHECK(Simulation::from_snapshot(w.world().canonical_state()).canonical_state()==
              w.world().canonical_state());
        const auto stock=w.world().life_support(receiver)->food_kg;
        w.steps(660-w.world().tick());
        CHECK(w.world().life_support(receiver)->food_kg==stock-5);
        CHECK(w.world().total_material_kg()==material);
        last_gen=w.generation();
        // The departure occurred many world commits earlier. Rolling
        // checkpoints alone must NOT erase the immutable pre-departure
        // world witness required by the independent custody verifier.
        CHECK(last_gen>predecessor_gen+3);
        CHECK(retained_snapshot_count(file,predecessor_gen)==1);
        CHECK(retained_snapshot_count(file,1)==1); // genesis replay anchor
    }
    {
        PersistentWorld reopened(file,Simulation(99));
        CHECK(reopened.generation()==last_gen);
        CHECK(reopened.world().dependents_physically_at(receiver)==1);
        CHECK(reopened.world().colony_ship(source)->crew==0);
        NpcAppearanceStore family(file,scope,seed,[&](auto id){
            return reopened.world().npc_records().count(id)!=0;
        });
        CHECK(family.current_residence(child)==receiver);
    }
    PersistentWorld::recover_copy(file,copy);
    {
        PersistentWorld recovered(copy,Simulation(100));
        CHECK(recovered.generation()==last_gen);
        CHECK(recovered.world().physical_dependents().at(child).location==receiver);
        CHECK(recovered.world().colony_ship(source)->crew==0);
        re::CheckpointStore region(copy,region_scope);
        CHECK(region.load()->revision==3);
        CHECK(region.load()->scheduler.colony("receiver").population==5);
        CHECK(retained_snapshot_count(copy,predecessor_gen)==1);
    }
    // A forged/lost predecessor must still fail closed, never silently
    // pronounce a child safely transferred based only on final census.
    delete_historical_snapshot(copy,predecessor_gen);
    denied([&]{PersistentWorld corrupted(copy,Simulation(101));});
    for(auto path:{file,copy}){
        std::filesystem::remove(path);
        std::filesystem::remove(path+"-wal");
        std::filesystem::remove(path+"-shm");
    }
    std::cout<<"PASS v59: ART02 born child plus native adult/child custody and ECON03 population moved atomically\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL v59 family: "<<e.what()<<'\n';return 1;}
}
