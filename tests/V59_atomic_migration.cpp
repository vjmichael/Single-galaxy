#include "galaxy/MedicalSocietyBridge.hpp"
#include "galaxy/RegionalModeAuthority.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <sqlite3.h>

using namespace galaxy;
namespace re=galaxy::regional_events;
static void check(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
template<class F>static void refused(F f,const char* why){bool caught=false;try{f();}catch(const std::exception&){caught=true;}check(caught,why);}
static Command command(std::uint64_t id,const PersistentWorld& w,EntityId actor,
                       EntityId dest,Action action,bool host=false){
    Command c{};c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
    c.issuer=host?Simulation::kServerAuthority:7;
    c.actor=actor;c.target=dest;c.action=action;
    if(host){c.transport_id=900;c.passenger_ids={1};}
    return c;
}
static void execute(PersistentWorld& w,std::uint64_t id){
    w.step();
    check(!w.world().events().empty()&&w.world().events().back().command_id==id &&
          w.world().events().back().result==Outcome::Applied,"host voyage command failed");
}
static void tamper(const std::string& path,const char* sql){
    sqlite3* db=nullptr;
    if(sqlite3_open_v2(path.c_str(),&db,SQLITE_OPEN_READWRITE,nullptr)!=SQLITE_OK)
        throw std::runtime_error("corruption fixture DB open failed");
    char* err=nullptr;const int rc=sqlite3_exec(db,sql,nullptr,nullptr,&err);
    if(rc!=SQLITE_OK){std::string msg=err?err:"SQL failed";sqlite3_free(err);
        sqlite3_close(db);throw std::runtime_error(msg);}
    sqlite3_close(db);
}
int main(){
    const auto path=(std::filesystem::temp_directory_path()/"sg_q6d2_atomic_migration.db").string();
    const auto replay=path+".replay",broken=path+".corrupt";
    const auto forged_region=path+".forgedregion",missing_effect=path+".missingeffect";
    const auto missing_move=path+".missingmove";
    std::filesystem::remove(path);std::filesystem::remove(replay);std::filesystem::remove(broken);
    std::filesystem::remove(forged_region);std::filesystem::remove(missing_effect);
    std::filesystem::remove(missing_move);
    EntityId origin=0,ferry=0,dest=0;
    try {
        {
            Simulation s(4062);s.add_system(2);
            origin=s.add_colony_ship(7,{0,0},100,6);
            ferry=s.add_colony_ship(7,{0,0},100,6);
            dest=s.add_colony_ship(7,{0,0},100,6);
            const auto gate=s.add_gate(1,{0,0},2,{5000,0});
            s.enable_individual_workforce();s.upgrade_assignment_snapshot_format();
            s.enable_medical_workforce_rules();
            PersistentWorld w(path,std::move(s));
            NpcAppearanceStore family(path,"migration",4062,[&](std::uint64_t n){
                return w.world().npc_records().count(n)!=0;});
            family.register_settlement(origin,6);
            family.register_settlement(dest,6);
            family.enroll_family_founder(1,origin);
            family.enroll_family_founder(13,dest);
            re::CheckpointStore region(path,"migration-region");
            re::Scheduler schedule;
            cold_colony::State a{};a.population=6;
            schedule.add_colony("origin",a);schedule.add_colony("destination",a);
            check(region.save(schedule,0)==1,"cannot initialize regional population");
            re::RegionalModeAuthority owner(region);
            check(owner.enroll().generation==1,"cold owner not enrolled");
            check(w.submit(command(100,w,dest,gate,Action::RequestJump)),"position destination");
            execute(w,100);
            w.steps(Simulation::kJumpTransitTicks+Simulation::kArrivalCloakTicks+1);
            check(w.world().find(dest)->system_id==2,"destination not in new star system");
            check(w.submit_host_passenger_action(command(101,w,ferry,origin,
                    Action::HostEmbarkPassengers,true)),"embark Bob");
            execute(w,101);
            check(w.world().npc_records().at(1).transport_id==900,"Bob not aboard");
            // Cannot migrate before physical arrival; no teleport or preemptive census.
            check(!w.submit_host_passenger_action(command(102,w,origin,dest,
                    Action::HostSettleMigrant,true)),"migration admitted before arrival");
            check(w.submit(command(103,w,ferry,gate,Action::RequestJump)),"ferry jump");
            execute(w,103);
            w.steps(Simulation::kJumpTransitTicks+Simulation::kArrivalCloakTicks+1);
            check(w.world().find(ferry)->system_id==2,"ferry did not travel");
            check(w.submit_host_passenger_action(command(104,w,ferry,dest,
                    Action::HostDisembarkPassengers,true)),"disembark Bob");
            execute(w,104);
            check(w.world().npc_records().at(1).home_ship==origin &&
                  w.world().npc_records().at(1).location==dest &&
                  w.world().npc_records().at(1).transport_id==0,
                  "arrival and residence should remain separate until migration");
            auto migration=command(105,w,origin,dest,Action::HostSettleMigrant,true);
            refused([&]{(void)w.submit_host_passenger_action(migration,FailPoint::AfterReceiptWrite);},
                    "migration submission precommit fault did not fail");
            check(w.world().npc_records().at(1).home_ship==origin,
                  "failed migration submission prematurely changed home");
            check(w.submit_host_passenger_action(migration),"submit atomic home migration");
            SocietyMigrationPlan plan{};
            plan.expected_world_generation=w.generation();
            plan.command_id=105;plan.npc_id=1;plan.transport_id=900;
            plan.origin=origin;plan.destination=dest;
            plan.expected_region_revision=1;plan.expected_owner_generation=1;
            plan.expected_origin_family_revision=0;
            plan.expected_destination_family_revision=0;
            plan.family_scope="migration";plan.region_scope="migration-region";
            plan.origin_colony="origin";plan.destination_colony="destination";
            plan.family_seed=4062;
            refused([&]{w.step();},
                    "ordinary simulation tick bypassed atomic family/region migration");
            check(w.world().npc_records().at(1).home_ship==origin &&
                  family.family_census(origin).current_population==6,
                  "generic step rejection leaked in-memory residence update");
            auto bad=plan;bad.expected_owner_generation=99;
            refused([&]{w.step_with_atomic_migration(bad);},"stale cold owner admitted");
            bad=plan;bad.expected_region_revision=5;
            refused([&]{w.step_with_atomic_migration(bad);},"stale regional checkpoint admitted");
            auto prep=owner.prepare("move-pause",re::SimulationOwner::Detailed,1,1,
                                    region.load()->scheduler.tick());
            check(prep.phase==re::TransitionPhase::ToDetailed,"handoff preparation failed");
            refused([&]{w.step_with_atomic_migration(plan);},
                    "prepared region handoff did not freeze society writer");
            check(owner.abort("move-pause",1).generation==2,"unfreeze did not rotate owner token");
            plan.expected_owner_generation=2;
            const auto before=w.world().canonical_state();
            refused([&]{w.step_with_atomic_migration(plan,FailPoint::AfterSnapshotWrite);},
                    "post-snapshot failure accepted migration");
            check(w.world().canonical_state()==before &&
                  family.current_residence(1)==origin &&
                  family.family_census(origin).current_population==6 &&
                  family.family_census(dest).current_population==6 &&
                  region.load()->revision==1,
                  "failed atomic move leaked people into destination");
            w.step_with_atomic_migration(plan);
            const auto& bob=w.world().npc_records().at(1);
            check(bob.home_ship==dest && bob.assigned_site==dest && bob.location==dest &&
                  bob.transport_id==0 && bob.id==1,"migrant Bob not living at destination");
            check(w.world().colony_ship(origin)->crew==5 &&
                  w.world().colony_ship(dest)->crew==7,"migration crew population did not conserve");
            check(family.current_residence(1)==dest &&
                  family.family_census(origin).current_population==5 &&
                  family.family_census(dest).current_population==7,
                  "migration family residence or census not conserved");
            const auto cp=region.load();
            check(cp && cp->revision==2 &&
                  cp->scheduler.colony("origin").population==5 &&
                  cp->scheduler.colony("destination").population==7,
                  "regional pair population changes did not conserve");
            check(reconcile_medical_society(w,family,path,"migration",origin,
                  region,"origin",2,w.generation()).reconciled,
                  "origin population not reconciled after migration");
            check(reconcile_medical_society(w,family,path,"migration",dest,
                  region,"destination",2,w.generation()).reconciled,
                  "destination population not reconciled after migration");
            refused([&]{w.step_with_atomic_migration(plan);},"duplicate migration was accepted");
            check(!w.submit_host_passenger_action(command(106,w,origin,dest,
                  Action::HostSettleMigrant,true)),"second migration of same NPC was allowed");
        }
        {
            PersistentWorld w(path,Simulation(123));
            check(w.world().npc_records().at(1).home_ship==dest &&
                  w.world().npc_records().at(1).location==dest,
                  "atomic move did not survive restart");
        }
        PersistentWorld::recover_copy(path,replay);
        {
            PersistentWorld w(replay,Simulation(123));
            NpcAppearanceStore family(replay,"migration",4062,[&](auto id){
                return w.world().npc_records().count(id)!=0;});
            re::CheckpointStore region(replay,"migration-region");
            check(w.world().npc_records().at(1).home_ship==dest &&
                  family.current_residence(1)==dest &&
                  reconcile_medical_society(w,family,replay,"migration",origin,
                      region,"origin",2,w.generation()).reconciled,
                  "genesis replay clone/regional/ART02 integrity failed");
        }
        PersistentWorld::recover_copy(path,broken);
        tamper(broken,"DROP TRIGGER npc_family_move_no_update; "
               "UPDATE npc_family_moves SET destination='999' WHERE event_id='105'");
        refused([&]{PersistentWorld x(broken,Simulation(123));},
                "forged family residence move accepted on restore");
        PersistentWorld::recover_copy(path,forged_region);
        tamper(forged_region,"UPDATE sg_regional_command_receipt SET request='forged' "
               "WHERE actor='society-migration-host' AND command_id='105'");
        refused([&]{PersistentWorld x(forged_region,Simulation(123));},
                "forged regional migration receipt accepted on restore");
        PersistentWorld::recover_copy(path,missing_effect);
        tamper(missing_effect,"DELETE FROM sg_society_migration WHERE command_id='105'");
        refused([&]{PersistentWorld x(missing_effect,Simulation(123));},
                "applied migration without world effect was accepted on restore");
        PersistentWorld::recover_copy(path,missing_move);
        tamper(missing_move,"DROP TRIGGER npc_family_move_no_delete; "
               "DELETE FROM npc_family_moves WHERE event_id='105'");
        refused([&]{PersistentWorld x(missing_move,Simulation(123));},
                "missing immutable family move accepted on restore");
        std::filesystem::remove(path);std::filesystem::remove(replay);
        std::filesystem::remove(broken);std::filesystem::remove(forged_region);
        std::filesystem::remove(missing_effect);std::filesystem::remove(missing_move);
        std::cout<<"PASS: disembark first, atomic ART02/ECON03 residence migration, "
                    "owner fences, failure rollback, restart and journal recovery\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"atomic migration failed: "<<e.what()<<'\n';return 1;}
}
