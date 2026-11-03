#include "galaxy/Persistence.hpp"
#include "galaxy/NpcAppearanceGenome.hpp"
#include "galaxy/RegionalCheckpointStore.hpp"
#include "galaxy/RegionalModeAuthority.hpp"
#include "galaxy/RegionalPopulationAudit.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <tuple>
using namespace galaxy;
namespace re=galaxy::regional_events;
static void check(bool test,const char* why){if(!test)throw std::runtime_error(why);}
template<class F>static void reject(F task,const char* why){
    bool rejected=false;try{task();}catch(const std::exception&){rejected=true;}
    check(rejected,why);
}
static Command command(const PersistentWorld& w,std::uint64_t id,Action action,
    EntityId actor,EntityId target,std::uint64_t transport){
    Command c{};c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
    c.action=action;c.actor=actor;c.target=target;c.transport_id=transport;
    c.passenger_ids={1,2,3,4};c.issuer=Simulation::kServerAuthority;
    return c;
}
int main(){
 const auto path=(std::filesystem::temp_directory_path()/"sg_v49_crew_execution.db").string();
 std::filesystem::remove(path);
 try{
    Simulation init(9123);
    const auto origin=init.add_colony_ship(7,{0,0},100,6);
    const auto ferry=init.add_colony_ship(7,{0,0},100,6);
    const auto rescue=init.add_colony_ship(7,{0,0},100,6);
    const auto second_rescue=init.add_colony_ship(7,{0,0},100,6);
    const auto dest=init.add_colony_ship(7,{0,0},100,6);
    init.enable_individual_workforce();init.upgrade_assignment_snapshot_format();
    init.enable_medical_workforce_rules();
    PersistentWorld w(path,std::move(init));
    std::vector<std::uint64_t> original_crew;
    for(const auto& [id,n]:w.world().npc_records())
        if(n.home_ship==ferry&&n.role==NpcRole::Ship&&n.assigned_site==ferry)
            original_crew.push_back(id);
    check(original_crew.size()==6,"expected real source operating crew");
    // Register the six native crew as ONE real, unfragmented home cohort.
    // The ferry rescue must not decrement their legal regional residence.
    {
        NpcAppearanceStore crew_registry(path,"operating-staff",9123,[&](auto id){
            return w.world().npc_records().count(id)!=0;});
        crew_registry.register_settlement(ferry,6);
        for(auto id:original_crew)crew_registry.enroll_family_founder(id,ferry);
    }
    re::CheckpointStore staff_region(path,"staff-region");
    re::Scheduler staff_scheduled;cold_colony::State staff_state{};
    staff_state.population=6;staff_scheduled.add_colony("ferry",staff_state);
    check(staff_region.save(staff_scheduled,0)==1,"staff regional checkpoint missing");
    re::RegionalModeAuthority staff_owner(staff_region);
    check(staff_owner.enroll().generation==1,"staff regional owner missing");
    const auto staff_before=verify_regional_population(w,path,ferry,
            {"operating-staff"},"staff-region","ferry",1,1,true);
    check(staff_before.registered==6&&staff_before.named_away_or_transit==0,
          "unrescued native crew did not match their home region");
    const auto a=(1ULL<<62)+141,b=(1ULL<<62)+142;
    for(const auto& [scope,seed,parent1,parent2] :
        std::vector<std::tuple<std::string,std::uint64_t,std::uint64_t,std::uint64_t>>{
            {"family-a",9123,1,2},{"family-b",9323,3,4}}){
        NpcAppearanceStore family(path,scope,seed,[&](auto id){
            return w.world().npc_records().count(id)!=0;});
        family.register_settlement(origin,6);family.register_settlement(dest,6);
        family.enroll_family_founder(parent1,origin);
        family.enroll_family_founder(parent2,origin);
    }
    cold_colony::State stock{};stock.population=6;
    for(const auto& scope:{"region-a","region-b"}){
        re::CheckpointStore region(path,scope);
        re::Scheduler scheduler;
        scheduler.add_colony("A",stock);scheduler.add_colony("B",stock);
        check(region.save(scheduler,0)==1,"regional checkpoint missing");
        re::RegionalModeAuthority auth(region);
        check(auth.enroll().generation==1,"regional ownership initialization");
    }
    SocietyBirthPlan birth{};
    birth.expected_world_generation=w.generation();birth.expected_region_revision=1;
    birth.expected_owner_generation=1;birth.family_scope="family-a";
    birth.family_seed=9123;birth.event_id=61;birth.child_id=a;
    birth.parent_a=1;birth.parent_b=2;birth.home_ship=origin;
    birth.region_scope="region-a";birth.regional_colony="A";
    w.step_with_atomic_dependent_birth(birth);
    birth.expected_world_generation=w.generation();birth.expected_region_revision=1;
    birth.region_scope="region-b";birth.family_scope="family-b";birth.family_seed=9323;birth.event_id=62;
    birth.child_id=b;birth.parent_a=3;birth.parent_b=4;
    w.step_with_atomic_dependent_birth(birth);
    DependentFamilyManifest f1{"family-a","region-a",9123,1,2,1,{a}};
    DependentFamilyManifest f2{"family-b","region-b",9323,3,4,1,{b}};
    check(w.submit_host_passenger_action(command(w,101,Action::HostEmbarkPassengers,
            ferry,origin,900)),"mixed-family boarding submit failed");
    MultiDependentTransportPlan board{};
    board.expected_world_generation=w.generation();board.command_id=101;
    board.transport_id=900;board.ferry=ferry;board.origin=origin;
    board.destination=dest;board.families={f1,f2};
    auto partial=board;partial.families={f1};
    reject([&]{w.step_with_multi_dependent_transport(partial);},
           "partial family boarding accepted");
    reject([&]{w.step_with_multi_dependent_transport(board,FailPoint::AfterReceiptWrite);},
           "mixed family boarding partial receipt escaped rollback");
    check(w.dependent_travel("family-a").empty()&&
          w.dependent_travel("family-b").empty(),"failed boarding wrote child berths");
    w.step_with_multi_dependent_transport(board);
    check(w.dependent_travel("family-a").size()==1&&
          w.dependent_travel("family-b").size()==1,
          "real mixed-family boarding was not committed for both families");
    {PersistentWorld reload(path,Simulation(9123));
     check(reload.world().npc_records().at(1).location==ferry &&
           reload.dependent_travel("family-b").at(0).status==1,
           "mixed-family boarding did not survive restart");}
    auto strand=command(w,102,Action::HostStrandFerry,ferry,ferry,900);
    strand.amount=2;
    check(w.submit_host_passenger_action(strand),"mixed ferry stranding refused");w.step();
    SplitRescueIntentPlan split{};
    split.intent_id="rescue-split-001";
    split.expected_world_generation=w.generation();
    split.source_ferry=ferry;split.source_transport=900;
    split.origin=origin;split.destination=dest;
    split.receivers={{rescue,901,{f1}},{second_rescue,902,{f2}}};
    auto bad=split;bad.receivers.pop_back();
    reject([&]{w.record_split_rescue_intent(bad);},
           "one-receiver fake split admission accepted");
    bad=split;bad.receivers[0].families={f1,f2};
    reject([&]{w.record_split_rescue_intent(bad);},
           "child family appeared in two rescue assignments");
    bad=split;bad.receivers[1].families[0].child_ids={b+1};
    reject([&]{w.record_split_rescue_intent(bad);},
           "forged second family dependent was admitted");
    bad=split;bad.receivers[1].new_transport_id=901;
    reject([&]{w.record_split_rescue_intent(bad);},
           "duplicate receiving voyage ID admitted");
    bad=split;bad.receivers[1].families[0].expected_owner_generation=90;
    reject([&]{w.record_split_rescue_intent(bad);},
           "stale region owner generation accepted");
    reject([&]{w.record_split_rescue_intent(split,FailPoint::AfterReceiptWrite);},
           "split intent header survived injected crash");
    {PersistentWorld reload(path,Simulation(9123));
     reject([&]{(void)reload.verify_split_rescue_intent(split.intent_id);},
            "rolled back split rescue intent visible after restart");}
    reject([&]{w.record_split_rescue_intent(split,FailPoint::AfterSnapshotWrite);},
           "split intent member rows survived injected crash");
    w.record_split_rescue_intent(split);
    const auto planned=w.verify_split_rescue_intent(split.intent_id);
    check(planned.families==2&&planned.adults==4&&planned.children==2&&
          planned.receiving_ferries==std::vector<EntityId>{rescue,second_rescue},
          "verified split roster not conserved");
    check(w.world().npc_records().at(1).location==ferry &&
          w.dependent_travel("family-b").at(0).status==1 &&
          w.dependent_travel("family-b").at(0).ferry==ferry,
          "planning an evacuation illegally moved passengers");
    reject([&]{w.record_split_rescue_intent(split);},
           "reused rescue intent ID unexpectedly accepted");
    check(w.submit_host_atomic_split_rescue(split,103,FailPoint::None,3),
          "split submission was rejected");
    {PersistentWorld pending(path,Simulation(9123));
     check(pending.world().npc_records().at(1).location==ferry,
           "pending rescue moved family before atomic commit");}
    reject([&]{w.step();},
           "ordinary world step bypassed atomic child custody on split rescue");
    reject([&]{w.step_with_atomic_split_rescue(split,103,FailPoint::AfterFirstSplitReceiver);},
           "after-first-receiver failure did not interrupt split transaction");
    {PersistentWorld restart(path,Simulation(9123));
     check(restart.world().npc_records().at(1).location==ferry&&
           restart.world().npc_records().at(3).location==ferry&&
           restart.dependent_travel("family-a").at(0).status==1&&
           restart.dependent_travel("family-b").at(0).status==1,
           "first receiver leaked through interrupted SQLite transaction");}
    reject([&]{w.step_with_atomic_split_rescue(split,103,FailPoint::AfterSnapshotWrite);},
           "post-snapshot crash allowed half-completed rescue");
    {PersistentWorld restart(path,Simulation(9123));
     check(restart.world().npc_records().at(2).location==ferry&&
           restart.world().npc_records().at(4).location==ferry,
           "post-snapshot failure did not roll back both families");}
    w.step_with_atomic_split_rescue(split,103);
    for(std::size_t i=0;i<original_crew.size();++i){
        const auto& crew=w.world().npc_records().at(original_crew[i]);
        const bool first=i<3;
        check(crew.location==(first?rescue:second_rescue)&&
              crew.transport_id==(first?901:902)&&
              crew.home_ship==ferry&&crew.assigned_site==ferry&&
              crew.role==NpcRole::Ship,"source operating crew was not conserved aboard rescue vessel");
    }
    const auto staff_after=verify_regional_population(w,path,ferry,
            {"operating-staff"},"staff-region","ferry",1,1,false);
    check(staff_after.registered==6&&staff_after.named_away_or_transit==6&&
          staff_after.named_present_for_duty==0,
          "physical crew evacuation invented/destroyed regional residents or on-duty staff");
    reject([&]{(void)verify_regional_population(w,path,ferry,
              {"operating-staff"},"staff-region","ferry",1,1,true);},
           "warm handoff preflight accepted absent operating crew");
    const auto staff_authority=staff_owner.state();
    reject([&]{staff_owner.prepare("invalid-warm",re::SimulationOwner::Detailed,
             staff_authority.generation,staff_authority.checkpoint_revision,
             staff_authority.boundary_tick);},
           "direct cold-to-detailed authority bypassed unbound operating crew" );
    check(w.world().passengers_aboard(rescue)==5&&
          w.world().passengers_aboard(second_rescue)==5,
          "rescue voyage adult manifests omitted operating crew");
    check(w.world().voyage_status(900)==5&&
          w.world().voyage_status(901)==1&&w.world().voyage_status(902)==1,
          "two receiving voyages not committed simultaneously");
    check(w.world().npc_records().at(1).location==rescue&&
          w.world().npc_records().at(2).location==rescue&&
          w.world().npc_records().at(3).location==second_rescue&&
          w.world().npc_records().at(4).location==second_rescue,
          "adults missing or duplicated after atomic split");
    check(w.world().npc_records().at(1).transport_id==901&&
          w.world().npc_records().at(3).transport_id==902,
          "adult voyage IDs incorrect after split");
    for(const auto& [scope,receiver,transport]:
          std::vector<std::tuple<std::string,EntityId,std::uint64_t>>{
            {"family-a",rescue,901},{"family-b",second_rescue,902}}){
        const auto legs=w.dependent_travel(scope);
        check(legs.size()==2,"child did not retain exact source + receiving leg");
        std::size_t old=0,newleg=0;
        for(const auto& leg:legs){
            if(leg.transport_id==900&&leg.status==2)++old;
            if(leg.transport_id==transport&&leg.ferry==receiver&&leg.status==1)++newleg;
        }
        check(old==1&&newleg==1,"child source berth not atomically transferred");
    }
    for(int i=0;i<7;++i)w.step(); // execution snapshot is pinned against pruning
    {PersistentWorld restored(path,Simulation(9123));
     check(restored.world().npc_records().at(1).location==rescue&&
           restored.world().npc_records().at(3).location==second_rescue,
           "reopening split world produced inconsistent adult custody");}
    const auto copy=path+".replay";std::filesystem::remove(copy);
    PersistentWorld::recover_copy(path,copy);
    {PersistentWorld recovered(copy,Simulation(9123));
     check(recovered.world().voyage_status(900)==5&&
           recovered.world().npc_records().at(4).transport_id==902,
           "independent genesis replay lost the two-receiver allocation");}
    // A forged second receiver cannot satisfy the independent child-adult ledger.
    {
        const auto damaged=path+".forged-second";std::filesystem::remove(damaged);
        sqlite3 *source=nullptr,*target=nullptr;
        check(sqlite3_open_v2(path.c_str(),&source,SQLITE_OPEN_READONLY,nullptr)==SQLITE_OK,
              "source backup unavailable");
        check(sqlite3_open(damaged.c_str(),&target)==SQLITE_OK,
              "target backup unavailable");
        auto* backup=sqlite3_backup_init(target,"main",source,"main");
        check(backup && sqlite3_backup_step(backup,-1)==SQLITE_DONE &&
              sqlite3_backup_finish(backup)==SQLITE_OK,"forged second ferry backup failed");
        const auto sql="UPDATE sg_dependent_travel SET ferry='"+std::to_string(rescue)+
                       "' WHERE family_scope='family-b' AND transport_id='902'";
        check(sqlite3_exec(target,sql.c_str(),nullptr,nullptr,nullptr)==SQLITE_OK,
              "forge second receiving ferry fixture failed");
        sqlite3_close(source);sqlite3_close(target);
        reject([&]{PersistentWorld forged(damaged,Simulation(9123));},
               "receiving-ferry child/guardian custody forgery accepted");
    }
    // Offline deletion cannot turn a partially recorded rescue into a valid one.
    for(const auto& table: {std::string("sg_split_rescue_execution"),
                            std::string("sg_dependent_transport_batch")}){
        const auto damaged=path+"."+table;std::filesystem::remove(damaged);
        sqlite3 *source=nullptr,*target=nullptr;
        check(sqlite3_open_v2(path.c_str(),&source,SQLITE_OPEN_READONLY,nullptr)==SQLITE_OK,
              "source backup unavailable");
        check(sqlite3_open(damaged.c_str(),&target)==SQLITE_OK,
              "target backup unavailable");
        auto* backup=sqlite3_backup_init(target,"main",source,"main");
        check(backup && sqlite3_backup_step(backup,-1)==SQLITE_DONE &&
              sqlite3_backup_finish(backup)==SQLITE_OK,"backup failed");
        const auto stmt="DROP TABLE "+table;
        check(sqlite3_exec(target,stmt.c_str(),nullptr,nullptr,nullptr)==SQLITE_OK,
              "offline table tamper failed");
        sqlite3_close(source);sqlite3_close(target);
        reject([&]{PersistentWorld forged(damaged,Simulation(9123));},
              "split rescue accepted deleted custody/execution evidence");
    }
    // Coordinated deletion of the execution row AND its counter must still be
    // rejected by the independent count of applied world split commands.
    {
        const auto damaged=path+".erased-receipt";std::filesystem::remove(damaged);
        sqlite3 *source=nullptr,*target=nullptr;
        check(sqlite3_open_v2(path.c_str(),&source,SQLITE_OPEN_READONLY,nullptr)==SQLITE_OK,
              "source ledger backup unavailable");
        check(sqlite3_open(damaged.c_str(),&target)==SQLITE_OK,
              "erased-receipt backup unavailable");
        auto* backup=sqlite3_backup_init(target,"main",source,"main");
        check(backup && sqlite3_backup_step(backup,-1)==SQLITE_DONE &&
              sqlite3_backup_finish(backup)==SQLITE_OK,"erased receipt backup failed");
        const char* tamper="DROP TRIGGER sg_split_rescue_execution_immutable_delete;"
            "DELETE FROM sg_split_rescue_execution;"
            "UPDATE world_meta SET split_rescue_execution_count=0;"
            "CREATE TRIGGER sg_split_rescue_execution_immutable_delete "
            "BEFORE DELETE ON sg_split_rescue_execution "
            "BEGIN SELECT RAISE(ABORT,'split execution immutable'); END;";
        check(sqlite3_exec(target,tamper,nullptr,nullptr,nullptr)==SQLITE_OK,
              "coordinated receipt deletion fixture failed");
        sqlite3_close(source);sqlite3_close(target);
        reject([&]{PersistentWorld forged(damaged,Simulation(9123));},
               "historical split rescue accepted erased execution receipt and anchor");
    }
    // Offline deletion of a native crew member receipt must not be silently
    // treated as a rescue of all the ferry's operating crew.
    for(const auto& tamper:std::vector<std::string>{
            "DROP TABLE sg_split_crew_execution;",
            "DROP TRIGGER sg_split_crew_execution_immutable_delete;"
            "DELETE FROM sg_split_crew_execution WHERE npc_id='"+
                 std::to_string(original_crew[0])+"';"
            "CREATE TRIGGER sg_split_crew_execution_immutable_delete BEFORE DELETE "
            "ON sg_split_crew_execution BEGIN SELECT RAISE(ABORT,'crew rescue receipt immutable'); END;"
        }){
        const auto damaged=path+".crew."+std::to_string(tamper.size());
        std::filesystem::remove(damaged);
        sqlite3* src=nullptr;sqlite3* dst=nullptr;
        check(sqlite3_open_v2(path.c_str(),&src,SQLITE_OPEN_READONLY,nullptr)==SQLITE_OK,
              "crew backup source unavailable");
        check(sqlite3_open(damaged.c_str(),&dst)==SQLITE_OK,
              "crew backup target unavailable");
        auto* backup=sqlite3_backup_init(dst,"main",src,"main");
        check(backup&&sqlite3_backup_step(backup,-1)==SQLITE_DONE&&
              sqlite3_backup_finish(backup)==SQLITE_OK,"crew tamper clone failed");
        check(sqlite3_exec(dst,tamper.c_str(),nullptr,nullptr,nullptr)==SQLITE_OK,
              "crew tamper preparation failed");
        sqlite3_close(src);sqlite3_close(dst);
        reject([&]{PersistentWorld corrupt(damaged,Simulation(9123));},
               "missing operating crew custody receipt accepted after restart");
    }
    std::cout<<"PASS v49: full operating crew and passengers rescued atomically"<<'\n';
    return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL v49: "<<e.what()<<'\n';return 1;}
}
