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
 const auto path=(std::filesystem::temp_directory_path()/"sg_e5b4_multifamily.db").string();
 std::filesystem::remove(path);
 try{
    Simulation init(9123);
    const auto origin=init.add_colony_ship(7,{0,0},100,6);
    const auto ferry=init.add_colony_ship(7,{0,0},100,6);
    const auto rescue=init.add_colony_ship(7,{0,0},100,6);
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
    auto emergency=command(w,103,Action::HostEvacuatePassengers,ferry,rescue,900);
    emergency.amount=901;
    check(w.submit_host_dependent_rescue_action(emergency),"mixed rescue could not queue");
    MultiDependentRescuePlan transfer{};
    transfer.expected_world_generation=w.generation();transfer.command_id=103;
    transfer.original_transport=900;transfer.rescue_transport=901;
    transfer.original_ferry=ferry;transfer.rescue_ferry=rescue;
    transfer.origin=origin;transfer.destination=dest;
    transfer.families={f1,f2};
    auto lost=transfer;lost.families={f1};
    reject([&]{w.step_with_multi_dependent_rescue(lost);},
           "omitted second family was evacuated");
    auto phantom=transfer;phantom.families[1].child_ids={b+1};
    reject([&]{w.step_with_multi_dependent_rescue(phantom);},
           "second family roster could be swapped to invented passenger");
    auto duplicate=transfer;duplicate.families[1].guardian_id=1;
    reject([&]{w.step_with_multi_dependent_rescue(duplicate);},
           "one parent duplicated into both family manifests");
    reject([&]{w.step_with_multi_dependent_rescue(transfer,FailPoint::AfterSnapshotWrite);},
           "failed mixed rescue published family custody");
    check(w.dependent_travel("family-a").at(0).status==1&&
          w.dependent_travel("family-b").at(0).status==1,
          "failed mixed rescue moved one child");
    w.step_with_multi_dependent_rescue(transfer);
    for(const auto& f:{f1,f2}){
        const auto legs=w.dependent_travel(f.family_scope);
        check(legs.size()==2&&legs[0].status==2&&legs[1].status==1&&
              legs[1].transport_id==901&&legs[1].transfer_from_transport==900,
              "family not atomically transferred to rescue ferry");
    }
    {PersistentWorld reload(path,Simulation(9123));
     check(reload.world().npc_records().at(3).location==rescue&&
           reload.dependent_travel("family-b").at(1).status==1,
           "rescue family custody failed restart");}
    auto leave=command(w,104,Action::HostDisembarkPassengers,rescue,dest,901);
    check(w.submit_host_passenger_action(leave),"mixed rescue arrival submission failed");
    auto arrival=board;arrival.boarding=false;arrival.command_id=104;
    arrival.transport_id=901;arrival.ferry=rescue;
    arrival.expected_world_generation=w.generation();
    w.step_with_multi_dependent_transport(arrival);
    for(const auto& f:{f1,f2}){
        const auto legs=w.dependent_travel(f.family_scope);
        check(legs.size()==2&&legs[1].status==2&&legs[1].termination_kind==0,
              "rescued child arrival not committed");
    }
    // A copied/offline-edited per-family batch is not independently valid.
    const auto forged=path+".forged";std::filesystem::remove(forged);
    {
      sqlite3 *src=nullptr,*dst=nullptr;
      check(sqlite3_open_v2(path.c_str(),&src,SQLITE_OPEN_READONLY,nullptr)==SQLITE_OK,
            "source backup unavailable");
      check(sqlite3_open(forged.c_str(),&dst)==SQLITE_OK,"forged backup unavailable");
      sqlite3_backup* backup=sqlite3_backup_init(dst,"main",src,"main");
      check(backup!=nullptr,"forged backup init unavailable");
      check(sqlite3_backup_step(backup,-1)==SQLITE_DONE&&
            sqlite3_backup_finish(backup)==SQLITE_OK,"forged backup error");
      char* error=nullptr;
      check(sqlite3_exec(dst,"DELETE FROM sg_dependent_transport_batch "
         "WHERE family_scope='family-b' AND command_id='103'",nullptr,nullptr,&error)
         ==SQLITE_OK,"batch alteration fixture failed");
      sqlite3_free(error);sqlite3_close(dst);sqlite3_close(src);
    }
    reject([&]{PersistentWorld bad(forged,Simulation(9123));},
           "missing one family's rescue batch accepted on reload");
    const auto replay=path+".replay";std::filesystem::remove(replay);
    PersistentWorld::recover_copy(path,replay);
    {PersistentWorld recovered(replay,Simulation(9123));
     check(recovered.dependent_travel("family-a").size()==2&&
           recovered.dependent_travel("family-b").size()==2,
           "mixed-family original and rescue journeys not independently replayable");}
    std::cout<<"PASS two families, 4 parents, 2 children, one ferry and rescue, rollback/replay\n";
    return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL multi-family rescue: "<<e.what()<<'\n';return 1;}
}
