#include "galaxy/Persistence.hpp"
#include "galaxy/NpcAppearanceGenome.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
#define CHECK(c) do{if(!(c))throw std::runtime_error(std::string("AUTO04D4D: ")+ #c+" at line "+std::to_string(__LINE__));}while(false)
static void sql(sqlite3* db,const std::string& s){
    char* err=nullptr;
    if(sqlite3_exec(db,s.c_str(),nullptr,nullptr,&err)!=SQLITE_OK){
        std::string message=err?err:"sqlite failure";sqlite3_free(err);
        throw std::runtime_error(message);
    }
}
int main(){
try{
    const auto path=(std::filesystem::temp_directory_path()/
       ("sg_auto04d4d_family_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".sqlite")).string();
    EntityId source=0,dest=0;
    const std::uint64_t child=(1ULL<<62)+312;
    {
        Simulation w(7311);
        source=w.add_colony_ship(7,{0,0},100,2);
        dest=w.add_colony_ship(7,{0,0},100,2);
        w.provision_colony_genesis(source,100,100,100,200,500);
        w.provision_colony_genesis(dest,100,100,100,200,500);
        w.enable_individual_workforce();
        w.upgrade_assignment_snapshot_format();
        w.enable_passenger_life_support();
        PersistentWorld host(path,std::move(w));
        const std::string scope="family-custody-73";constexpr std::uint64_t seed=9811;
        {
            NpcAppearanceStore family(path,scope,seed,
                [&](std::uint64_t id){return host.world().npc_records().count(id)!=0;});
            family.register_settlement(source,2);
            family.register_settlement(dest,2);
            for(std::uint64_t id=1;id<=2;++id)family.enroll_family_founder(id,source);
            for(std::uint64_t id=3;id<=4;++id)family.enroll_family_founder(id,dest);
            const auto proof=family.record_birth(910,child,1,2,source,1,0,
                [](auto,auto,auto,auto){return true;});
            CHECK(proof.child_id==child && proof.parent_a==1 && proof.parent_b==2);
            CHECK(family.family_census(source).current_population==3);
            CHECK(family.current_residence(child)==source);
        }
        sqlite3* db=nullptr;
        CHECK(sqlite3_open(path.c_str(),&db)==SQLITE_OK);
        // Bridge fixture mirrors the independently attested ART02 birth.
        // In production, the host world+regional birth transaction creates it.
        sql(db,"CREATE TABLE sg_med_society_birth ("
              "event_id TEXT PRIMARY KEY,world_generation INTEGER NOT NULL,"
              "tick INTEGER NOT NULL,family_scope TEXT NOT NULL,"
              "region_scope TEXT NOT NULL,region_revision INTEGER NOT NULL,"
              "owner_generation INTEGER NOT NULL,home_ship TEXT NOT NULL,"
              "regional_colony TEXT NOT NULL,child_id TEXT NOT NULL UNIQUE,"
              "parent_a TEXT NOT NULL,parent_b TEXT NOT NULL,"
              "family_revision INTEGER NOT NULL)");
        const std::string bridge="INSERT INTO sg_med_society_birth VALUES "
          "('910',1,1,'"+scope+"','region-73',1,1,'"+std::to_string(source)+
          "','origin','"+std::to_string(child)+"','1','2',1)";
        sql(db,bridge);
        sqlite3_close(db);
        colony_family::DeparturePlan plan;
        plan.source=source;plan.receiver=dest;plan.family_scope=scope;
        plan.family_seed=seed;plan.adult_ids={1,2};
        plan.children={{child,1}};
        auto ok=host.audit_colony_family_departure(plan);
        CHECK(ok.complete_adult_roster);
        CHECK(ok.complete_child_roster);
        CHECK(ok.parent_or_guardian_custody);
        CHECK(ok.birth_provenance);
        CHECK(ok.census_reconciled);
        CHECK(ok.receiving_supplies);
        CHECK(ok.ready_for_transaction);
        CHECK(!ok.evacuated);
        CHECK(ok.dependent_children==1);
        plan.intent_id=50001;
        plan.expected_world_generation=host.generation();
        bool injected=false;
        try{host.prepare_host_colony_family_departure(plan,FailPoint::AfterReceiptWrite);}
        catch(const std::runtime_error&){injected=true;}
        CHECK(injected);
        CHECK(sqlite3_open(path.c_str(),&db)==SQLITE_OK);
        sqlite3_stmt* count=nullptr;
        CHECK(sqlite3_prepare_v2(db,"SELECT COUNT(*) FROM sg_colony_family_departure_prepare",
              -1,&count,nullptr)==SQLITE_OK);
        CHECK(sqlite3_step(count)==SQLITE_ROW && sqlite3_column_int(count,0)==0);
        sqlite3_finalize(count);sqlite3_close(db);
        CHECK(host.prepare_host_colony_family_departure(plan));
        CHECK(host.prepare_host_colony_family_departure(plan)); // identical retry
        auto reused=plan;reused.receiver=source;
        CHECK(!host.prepare_host_colony_family_departure(reused));
        auto stale=plan;stale.expected_world_generation=plan.expected_world_generation+1;
        CHECK(!host.prepare_host_colony_family_departure(stale));
        CHECK(host.world().colony_departures().empty()); // preparation is not rescue
        CHECK(host.world().npc_records().size()==4);
        CHECK(!host.audit_colony_family_departure(plan).evacuated);
        auto missing=plan;missing.children.clear();
        CHECK(!host.audit_colony_family_departure(missing).ready_for_transaction);
        auto forged=plan;forged.children={{child+1,1}};
        CHECK(!host.audit_colony_family_departure(forged).ready_for_transaction);
        auto wrong_guard=plan;wrong_guard.children={{child,3}};
        CHECK(!host.audit_colony_family_departure(wrong_guard).ready_for_transaction);
        auto duplicate=plan;duplicate.children={{child,1},{child,1}};
        CHECK(!host.audit_colony_family_departure(duplicate).ready_for_transaction);
        auto missing_adult=plan;missing_adult.adult_ids={1};
        CHECK(!host.audit_colony_family_departure(missing_adult).ready_for_transaction);
        // A real child is never a fabricated world NPC or a free rescue action.
        CHECK(host.world().npc_records().size()==4);
        CHECK(host.world().colony_departures().empty());
        // Corrupt the independent bridge: ART02 birth alone cannot authorize a move.
        CHECK(sqlite3_open(path.c_str(),&db)==SQLITE_OK);
        sql(db,"DELETE FROM sg_med_society_birth WHERE event_id='910'");
        sqlite3_close(db);
        const auto mismatch=host.audit_colony_family_departure(plan);
        CHECK(!mismatch.ready_for_transaction);
        CHECK(!mismatch.evacuated);
    }
    std::filesystem::remove(path);
    std::filesystem::remove(path+"-wal");
    std::filesystem::remove(path+"-shm");
    std::cout<<"PASS 04D.4D1: ART02 born identity, complete guardian and census preflight, forged proofs rejected\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
