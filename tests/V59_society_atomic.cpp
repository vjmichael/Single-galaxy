#include "galaxy/MedicalSocietyBridge.hpp"
#include "galaxy/RegionalModeAuthority.hpp"
#include <sqlite3.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace galaxy;
namespace re=galaxy::regional_events;
static void check(bool v,const char* what){if(!v)throw std::runtime_error(what);}
template<class F>static void rejected(F f,const char* why){
    bool threw=false;try{f();}catch(const std::exception&){threw=true;}check(threw,why);
}
static Command host(std::uint64_t id,Action action,EntityId ship,EntityId target,
                    std::uint64_t voyage,std::vector<std::uint64_t> ids,
                    std::int64_t count,std::uint64_t tick){
    Command c{};c.id=id;c.sequence=id;c.issuer=Simulation::kServerAuthority;
    c.execute_tick=tick;c.action=action;c.actor=ship;c.target=target;
    c.transport_id=voyage;c.passenger_ids=std::move(ids);c.amount=count;
    return c;
}
static void sql_change(const std::string& path,const char* sql){
    sqlite3* db=nullptr;
    check(sqlite3_open_v2(path.c_str(),&db,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE,nullptr)==SQLITE_OK,
          "open DB for corruption fixture");
    char* error=nullptr;const auto rc=sqlite3_exec(db,sql,nullptr,nullptr,&error);
    if(rc!=SQLITE_OK){std::string msg=error?error:"SQLite failure";
        sqlite3_free(error);sqlite3_close(db);throw std::runtime_error(msg);}
    sqlite3_close(db);
}
static std::uint64_t count_rows(const std::string& path,const char* table){
    sqlite3* db=nullptr;
    check(sqlite3_open_v2(path.c_str(),&db,SQLITE_OPEN_READONLY,nullptr)==SQLITE_OK,"open read db");
    std::string sql="SELECT count(*) FROM "+std::string(table);
    sqlite3_stmt* q=nullptr;
    check(sqlite3_prepare_v2(db,sql.c_str(),-1,&q,nullptr)==SQLITE_OK,"query receipt table missing");
    check(sqlite3_step(q)==SQLITE_ROW,"receipt count failed");
    const auto n=static_cast<std::uint64_t>(sqlite3_column_int64(q,0));
    sqlite3_finalize(q);sqlite3_close(db);return n;
}
int main(){
    const auto db=(std::filesystem::temp_directory_path()/"sg_society_atomic_q6b.db").string();
    const auto copy=db+".replay";
    const auto legacy=db+".legacy";
    std::filesystem::remove(db);std::filesystem::remove(copy);std::filesystem::remove(legacy);
    try{
        // A genuine legacy census with >=1 CHECK must migrate in place without
        // losing its row. After migration, a final recorded death may reach 0.
        sql_change(legacy,"CREATE TABLE npc_family_census("
            "scope TEXT NOT NULL,settlement TEXT NOT NULL,"
            "initial_population INTEGER NOT NULL CHECK(initial_population>=1),"
            "current_population INTEGER NOT NULL CHECK(current_population>=1),"
            "revision INTEGER NOT NULL CHECK(revision>=0),"
            "last_tick INTEGER NOT NULL CHECK(last_tick>=0),PRIMARY KEY(scope,settlement));"
            "INSERT INTO npc_family_census VALUES ('legacy','1',1,1,0,0);");
        {
            NpcAppearanceStore f(legacy,"legacy",4006,[](auto id){return id==1;});
            check(f.family_census(1).current_population==1,"legacy census migration lost row");
            f.enroll_family_founder(1,1);
            f.record_death(100,1,1,1,0,[](auto,auto,auto){return true;});
            check(f.family_census(1).current_population==0,
                "legacy >=1 census could not represent colony extinction");
        }
        EntityId home=0,ferry=0,ems=0;
        std::uint64_t end_generation=0;
        {
            Simulation s(4006);
            home=s.add_colony_ship(7,{0,0},100,20);
            ferry=s.add_colony_ship(7,{0,0},100,4);
            ems=s.add_colony_ship(7,{0,0},100,8);
            s.enable_individual_workforce();s.upgrade_assignment_snapshot_format();
            s.enable_medical_workforce_rules();
            PersistentWorld w(db,std::move(s));
            NpcAppearanceStore family(db,"society",4006,[&](auto id){
                return w.world().npc_records().count(id)!=0;});
            family.register_settlement(home,20);family.register_settlement(ferry,4);
            for(auto npc:{1U,2U,3U})family.enroll_family_founder(npc,home);
            for(auto npc:{21U,22U,23U,24U})family.enroll_family_founder(npc,ferry);
            check(w.submit_host_passenger_action(host(600,Action::HostEmbarkPassengers,
                  ferry,home,900,{1,2,3},0,1)),"could not queue embarkation");
            w.step();
            constexpr auto child=(1ULL<<62)+101;
            family.record_birth(10001,child,1,2,home,1,0,
                [](auto,auto,auto,auto){return true;});
            re::CheckpointStore region(db,"society-region");
            re::Scheduler scheduler;
            cold_colony::State h{};h.population=21;
            cold_colony::State f{};f.population=4;
            scheduler.add_colony("home",h);scheduler.add_colony("ferry",f);
            check(region.save(scheduler,0)==1,"failed regional baseline");
            re::RegionalModeAuthority ownership(region);
            const auto enrolled=ownership.enroll();
            check(enrolled.generation==1 && enrolled.checkpoint_revision==1,
                "regional owner did not enroll");
            const auto v=reconcile_medical_society(w,family,db,"society",home,
                region,"home",1,w.generation());
            check(v.reconciled && v.family_population==21,"initial world/family/region proof failed");
            check(w.submit_host_passenger_action(host(701,Action::HostResolveMixedFerryLoss,
                ferry,ems,900,{1,2,3,21,22,23,24},2,2)),
                "could not queue terminal incident");
            SocietyAtomicPlan plan{};
            plan.expected_world_generation=w.generation();
            plan.command_id=701;plan.expected_region_revision=1;
            plan.expected_owner_generation=1;plan.family_seed=4006;
            plan.family_scope="society";plan.region_scope="society-region";
            plan.deaths={{10003,3,home,1,"home"},{10021,21,ferry,0,"ferry"},
                         {10022,22,ferry,0,"ferry"},{10023,23,ferry,0,"ferry"},
                         {10024,24,ferry,0,"ferry"}};
            auto bad_owner=plan;bad_owner.expected_owner_generation=2;
            rejected([&]{w.step_with_atomic_society(bad_owner);},"stale owner token accepted");
            check(w.world().tick()==1 && family.family_census(home).current_population==21 &&
                region.load()->revision==1,"stale-owner rejection created partial effects");
            auto bad_family=plan;bad_family.deaths.front().expected_family_revision=9;
            rejected([&]{w.step_with_atomic_society(bad_family);},"stale census token accepted");
            check(family.family_census(home).current_population==21 && region.load()->revision==1,
                "stale-census rejection created partial effects");
            auto bad_manifest=plan;bad_manifest.deaths.pop_back();
            rejected([&]{w.step_with_atomic_society(bad_manifest);},"partial casualty manifest accepted");
            auto bad_binding=plan;bad_binding.deaths.front().regional_colony="ferry";
            rejected([&]{w.step_with_atomic_society(bad_binding);},
                "foreign regional colony was accepted for home casualty");
            auto bad_region=plan;bad_region.expected_region_revision=3;
            rejected([&]{w.step_with_atomic_society(bad_region);},
                "stale regional checkpoint revision was accepted");
            rejected([&]{w.step_with_atomic_society(plan,FailPoint::AfterSnapshotWrite);},
                "injected post-effect SQLite failure not raised");
            check(w.world().tick()==1 && w.world().npc_records().count(3)!=0 &&
                family.family_census(home).current_population==21 &&
                family.family_census(ferry).current_population==4 &&
                region.load()->revision==1 &&
                count_rows(db,"npc_family_deaths")==0 &&
                count_rows(db,"sg_regional_command_receipt")==0,
                "post-effect failure orphaned family, regional or world record");
            w.step_with_atomic_society(plan); // Safe retry, same event identity.
            check(w.world().tick()==2 && w.world().npc_casualties().size()==5 &&
                w.world().npc_records().count(1)!=0 &&
                family.family_census(home).current_population==20 &&
                family.family_census(ferry).current_population==0,
                "atomic casualty population loss failed");
            check(region.load()->revision==2 &&
                region.load()->scheduler.colony("home").population==20 &&
                region.load()->scheduler.colony("ferry").population==0 &&
                ownership.state().checkpoint_revision==2 &&
                count_rows(db,"sg_regional_command_receipt")==1 &&
                count_rows(db,"sg_med_society_effect")==1 &&
                count_rows(db,"npc_family_deaths")==5,
                "atomic effect receipts or regional conservation failed");
            const auto p=reconcile_medical_society(w,family,db,"society",home,
                region,"home",2,w.generation());
            const auto q=reconcile_medical_society(w,family,db,"society",ferry,
                region,"ferry",2,w.generation());
            check(p.reconciled && q.reconciled && p.dead_named==1 &&
                p.dependent_births==1 && q.dead_named==4 &&
                p.family_population==20 && q.family_population==0,
                "cross-colony post-incident reconciliation mismatch");
            rejected([&]{w.step_with_atomic_society(plan);},
                "stale world generation reused atomic command");
            end_generation=w.generation();
            PersistentWorld::recover_copy(db,copy);
        }
        {
            PersistentWorld w(db,Simulation(1));
            NpcAppearanceStore family(db,"society",4006,[&](auto id){
                return w.world().npc_records().count(id)!=0;});
            re::CheckpointStore region(db,"society-region");
            check(w.generation()==end_generation && region.load()->revision==2,
                "reopen lost atomic incident generation/revision");
            check(reconcile_medical_society(w,family,db,"society",home,
                region,"home",2,w.generation()).reconciled,
                "reopen lost canonical family and regional reconciliation");
        }
        {
            PersistentWorld w(copy,Simulation(1));
            NpcAppearanceStore family(copy,"society",4006,[&](auto id){
                return w.world().npc_records().count(id)!=0;});
            re::CheckpointStore region(copy,"society-region");
            check(w.generation()==end_generation &&
                reconcile_medical_society(w,family,copy,"society",home,
                region,"home",2,w.generation()).reconciled &&
                count_rows(copy,"sg_med_society_effect")==1,
                "genesis replay recovery altered atomic society receipts");
        }
        // Reopen after deliberately changing a separately recorded effect.
        // This is a copy, never the original authoritative source.
        sql_change(copy,"UPDATE sg_med_society_effect SET casualty_count=4 WHERE command_id='701'");
        rejected([&]{PersistentWorld damaged(copy,Simulation(1));},
                 "corrupt cross-store casualty receipt accepted after restart");
        std::filesystem::remove(db);std::filesystem::remove(copy);std::filesystem::remove(legacy);
        std::cout<<"PASS q6b atomic world/family/two-region-colony casualty transaction + rollback + restart + genesis replay\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL q6b: "<<e.what()<<'\n';
        std::filesystem::remove(db);std::filesystem::remove(copy);std::filesystem::remove(legacy);
        return 1;
    }
}
