#include "galaxy/Persistence.hpp"
#include "galaxy/NpcAppearanceGenome.hpp"
#include "galaxy/RegionalCheckpointStore.hpp"
#include "galaxy/RegionalModeAuthority.hpp"
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
 const auto path=(std::filesystem::temp_directory_path()/"sg_v47_split_intent.db").string();
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
    for(int i=0;i<7;++i)w.step(); // historical plan generation must stay pinned
    {PersistentWorld reload(path,Simulation(9123));
     check(reload.verify_split_rescue_intent(split.intent_id).children==2,
           "split manifest not durable after world advancement and restart");}
    const auto copy=path+".replay";std::filesystem::remove(copy);
    PersistentWorld::recover_copy(path,copy);
    {PersistentWorld recovered(copy,Simulation(9123));
     check(recovered.verify_split_rescue_intent(split.intent_id).adults==4,
           "split preflight receipt did not survive independent journal recovery");}
    for(const auto& [suffix,edit]:std::vector<std::pair<std::string,std::string>>{
        {"child","DELETE FROM sg_split_rescue_child WHERE family_scope='family-b'"},
        {"header","DELETE FROM sg_split_rescue_intent"},
        {"counter","UPDATE world_meta SET split_rescue_plan_count=0"},
        {"transport","UPDATE sg_split_rescue_receiver SET new_transport='903' "
                      "WHERE ferry='"+std::to_string(second_rescue)+"'"},
        {"region","UPDATE sg_split_rescue_family SET region_scope='region-fake' "
                   "WHERE family_scope='family-b'"}}){
        const auto damaged=path+"."+suffix;std::filesystem::remove(damaged);
        sqlite3 *src=nullptr,*dst=nullptr;
        check(sqlite3_open_v2(path.c_str(),&src,SQLITE_OPEN_READONLY,nullptr)==SQLITE_OK,
              "source SQLite backup unavailable");
        check(sqlite3_open(damaged.c_str(),&dst)==SQLITE_OK,
              "damaged sqlite backup unavailable");
        sqlite3_backup* backup=sqlite3_backup_init(dst,"main",src,"main");
        check(backup!=nullptr,"sqlite backup init failed");
        check(sqlite3_backup_step(backup,-1)==SQLITE_DONE&&
              sqlite3_backup_finish(backup)==SQLITE_OK,"sqlite backup failed");
        char* error=nullptr;
        // Deliberate offline tampering defeats local immutability triggers.
        const std::string first="DROP TRIGGER sg_split_rescue_intent_immutable_delete;"
            "DROP TRIGGER sg_split_rescue_child_immutable_delete;"
            "DROP TRIGGER sg_split_rescue_receiver_immutable_update;"
            "DROP TRIGGER sg_split_rescue_family_immutable_update;";
        const auto rc=sqlite3_exec(dst,(first+edit).c_str(),nullptr,nullptr,&error);
        if(rc!=SQLITE_OK)std::cerr<<"attack fixture "<<suffix<<": "
                                  <<(error?error:"unknown")<<'\n';
        check(rc==SQLITE_OK,"attack fixture failed");
        sqlite3_free(error);
        // Attack succeeds past local triggers. Reinstall them, so recovery must
        // detect the historical inconsistency independently of trigger loss.
        for(const auto* table:{"sg_split_rescue_intent","sg_split_rescue_receiver",
                               "sg_split_rescue_family","sg_split_rescue_child"}){
            for(const auto* type:{"UPDATE","DELETE"}){
                const auto name=std::string(table)+
                    (std::string(type)=="UPDATE"?"_immutable_update":"_immutable_delete");
                const auto ddl="CREATE TRIGGER IF NOT EXISTS "+name+" BEFORE "+type+
                    " ON "+table+" BEGIN SELECT RAISE(ABORT,'split intent immutable'); END";
                check(sqlite3_exec(dst,ddl.c_str(),nullptr,nullptr,&error)==SQLITE_OK,
                      "restore tamper guard failed");
                sqlite3_free(error);error=nullptr;
            }
        }
        sqlite3_close(dst);sqlite3_close(src);
        reject([&]{PersistentWorld forged(damaged,Simulation(9123));},
               "offline altered rescue split ledger accepted on restart");
    }
    std::cout<<"PASS v47a two-family split-ferry intent, rollback/restart/replay/tamper refusal\n";
    return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL v47a: "<<e.what()<<'\n';return 1;}
}
