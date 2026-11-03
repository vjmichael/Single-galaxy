#include "galaxy/MedicalSocietyBridge.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace galaxy;
namespace re=galaxy::regional_events;
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class F>static void denied(F f,const char* why){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,why);}
static Command cmd(std::uint64_t id,Action a,EntityId actor,EntityId target,
                   std::uint64_t voyage,std::vector<std::uint64_t> ids,std::int64_t count,
                   std::uint64_t tick){
    Command c{};c.id=id;c.sequence=id;c.issuer=Simulation::kServerAuthority;
    c.execute_tick=tick;c.action=a;c.actor=actor;c.target=target;
    c.transport_id=voyage;c.passenger_ids=std::move(ids);c.amount=count;
    return c;
}
static void player(PersistentWorld& w,std::uint64_t id,EntityId actor,Action a,
                   EntityId target=0,std::int64_t amount=1,int selector=0){
    Command c{};c.id=id;c.sequence=id;c.issuer=7;c.execute_tick=w.world().tick()+1;
    c.actor=actor;c.action=a;c.target=target;c.amount=amount;c.thrust_x=selector;
    check(w.submit(c),"player command refused");
    w.step();
    check(w.world().events().back().result==Outcome::Applied,"player command failed");
}
static void run(PersistentWorld& w,Command c){
    bool ok=false;
    if(c.action==Action::HostTriageRescued)ok=w.submit_host_medical_action(c);
    else ok=w.submit_host_passenger_action(c);
    check(ok,"host command refused");w.step();
    check(w.world().events().back().result==Outcome::Applied,"host action not applied");
}
int main(){
    const auto root=(std::filesystem::temp_directory_path()/"sg_med_society_v40").string();
    const auto db=root+".db",copy=root+".replay";
    std::filesystem::remove(db);std::filesystem::remove(copy);
    try {
        EntityId home=0,ferry=0,ems=0,ore=0;
        std::uint64_t expected_generation=0;
        {
            Simulation s(4006);
            home=s.add_colony_ship(7,{0,0},100,20);
            ferry=s.add_colony_ship(7,{0,0},100,4);
            ems=s.add_colony_ship(7,{0,0},100,8);
            s.provision_colony_genesis(home,1000,3000,1000,1000,1000);
            ore=s.add_asteroid({2000,0},90);
            s.enable_individual_workforce();s.upgrade_assignment_snapshot_format();
            s.enable_medical_workforce_rules();
            check(s.canonical_state().rfind("galaxy-kernel-v40|",0)==0,
                "explicit v40 workforce snapshot not enabled");
            check(Simulation::from_snapshot(s.canonical_state()).canonical_state()==s.canonical_state(),
                "v40 genesis snapshot not loadable");
            PersistentWorld w(db,std::move(s));
            NpcAppearanceStore family(db,"settlement-home",4006,[&](auto id){
                return w.world().npc_records().count(id)!=0;});
            family.register_settlement(home,20);
            family.enroll_family_founder(1,home);
            family.enroll_family_founder(2,home);
            family.enroll_family_founder(3,home);
            run(w,cmd(600,Action::HostEmbarkPassengers,ferry,home,900,{1,2,3},0,1));
            constexpr auto child=(1ULL<<62)+100;
            const auto birth=family.record_birth(10001,child,1,2,home,1,0,
                [](auto,auto,auto,auto){return true;});
            check(birth.child_id==child && family.family_census(home).current_population==21,
                  "real family birth receipt not committed");
            re::CheckpointStore region(db,"region-home");
            re::Scheduler scheduler;
            cold_colony::State h{};h.population=21;
            scheduler.add_colony("home",h);
            const auto rev1=region.save(scheduler,0);
            check(rev1==1,"regional checkpoint revision not established");
            const auto before=reconcile_medical_society(w,family,db,"settlement-home",
                home,region,"home",rev1,w.generation());
            check(before.reconciled && before.family_population==21 &&
                before.dependent_births==1 && before.living_named==20 &&
                before.ready_workers[0]==17 && before.unavailable==3 &&
                before.affected_dependents==1,
                "boarding/family/regional population projection contradictory");
            run(w,cmd(701,Action::HostResolveMixedFerryLoss,ferry,ems,900,
                {1,2,3,21,22,23,24},2,2));
            check(w.world().npc_casualties().count(3)==1 &&
                w.world().npc_records().count(3)==0,"canonical death absent");
            // Family remains a separate store: it must refuse claiming an
            // integrated census while the actual family death receipt is absent.
            denied([&]{(void)reconcile_medical_society(w,family,db,"settlement-home",
                home,region,"home",rev1,w.generation());},"missing death receipt was accepted");
            const auto death=family.record_death(10003,3,home,2,1,
                [&](auto settlement,auto npc,auto tick){
                    auto d=w.world().npc_casualties().find(npc);
                    return settlement==home && d!=w.world().npc_casualties().end() &&
                        d->second.death_tick==tick;});
            check(death.npc_id==3 && family.family_census(home).current_population==20,
                "family casualty not reconciled");
            // A regional population change is independently journaled; the
            // view remains fail-closed until that checkpoint revision advances.
            denied([&]{(void)reconcile_medical_society(w,family,db,"settlement-home",
                home,region,"home",rev1,w.generation());},"stale regional headcount accepted");
            scheduler.schedule_population("home",20,2);
            scheduler.advance_to(2);
            const auto rev2=region.save(scheduler,rev1);
            check(rev2==2,"regional population update not durable");
            const auto ready=reconcile_medical_society(w,family,db,"settlement-home",
                home,region,"home",rev2,w.generation());
            check(ready.family_population==20 && ready.living_named==19 &&
                ready.dead_named==1 && ready.dependent_births==1 &&
                ready.affected_dependents==1 && ready.traveling>=2,
                "home mortality, survivors, dependents or travel double-counted");
            // Bob now needs medical care; this must immediately reduce workforce
            // readiness without mutating family or regional population counts.
            run(w,cmd(702,Action::HostTriageRescued,ems,ems,701,{1,2},2,3));
            const auto injured=reconcile_medical_society(w,family,db,"settlement-home",
                home,region,"home",rev2,w.generation());
            check(injured.reconciled && injured.medically_unavailable>=2 &&
                injured.family_population==20 && injured.affected_dependents==1,
                "injury did not affect actual medical workforce projection");
            denied([&]{(void)reconcile_medical_society(w,family,db,"settlement-home",
                home,region,"home",rev1,w.generation());},"stale checkpoint revision accepted");
            denied([&]{(void)reconcile_medical_society(w,family,db,"settlement-home",
                home,region,"home",rev2,w.generation()-1);},
                "stale authoritative generation accepted");
            // Injured passengers can leave EMS, but cannot be selected as
            // new farm labor. The actual player workforce assignment (not
            // the read-only audit) must skip Bob and the second patient.
            run(w,cmd(703,Action::HostDisembarkPassengers,ems,home,701,{1,2},0,4));
            check(!w.world().npc_available_for_duty(1) &&
                w.world().npc_records().at(1).location==home,
                "injured Bob should be physically home and medically unavailable");
            player(w,801,home,Action::Mine,ore,40);
            player(w,802,home,Action::RefineOre,0,40);
            player(w,803,home,Action::FabricateModule,0,20);
            player(w,804,home,Action::ConstructOutpost);
            const auto site=w.world().built_outposts().begin()->first;
            player(w,805,home,Action::ConstructFarm,site);
            for(int selector=1;selector<=5;++selector)
                player(w,805+selector,home,Action::TransferFarmStock,site,
                       selector==2?900:200,selector);
            player(w,811,home,Action::AssignFarmWorkers,site,1);
            check(w.world().farm(site)->workers==1 &&
                w.world().npc_records().at(1).role==NpcRole::Ship &&
                w.world().npc_records().at(2).role==NpcRole::Ship &&
                w.world().npc_records().at(4).role==NpcRole::Farm &&
                w.world().npc_available_for_duty(4),
                "v40 actual workforce dispatcher assigned injured Bob");
            w.steps(55);
            check(w.world().farm(site)->production_cycles>0 &&
                  w.world().npc_records().at(4).farm_xp>0,
                  "healthy replacement worker did not sustain actual farm");
            const auto farm_audit=reconcile_medical_society(w,family,db,
                "settlement-home",home,region,"home",rev2,w.generation());
            check(farm_audit.ready_workers[1]==1 &&
                farm_audit.medically_unavailable==2 && farm_audit.affected_dependents==1,
                "farm labor, medical injury, or family dependent audit diverged");
            expected_generation=w.generation();
            check(expected_generation!=0,"generation is not durable");
            // Copy-and-replay through the original world journal must preserve
            // the extra ART02 and ECON03 tables and the same read-only proof.
            PersistentWorld::recover_copy(db,copy);
            {
                PersistentWorld recovered(copy,Simulation(1));
                NpcAppearanceStore copy_family(copy,"settlement-home",4006,[&](auto id){
                    return recovered.world().npc_records().count(id)!=0;});
                re::CheckpointStore copy_region(copy,"region-home");
                const auto proof=reconcile_medical_society(recovered,copy_family,copy,
                    "settlement-home",home,copy_region,"home",rev2,recovered.generation());
                check(proof.reconciled && proof.family_population==20 &&
                    recovered.world().canonical_state()==w.world().canonical_state(),
                    "independent world replay dropped population reconciliation");
            }
        }
        {
            PersistentWorld w(db,Simulation(1));
            NpcAppearanceStore family(db,"settlement-home",4006,[&](auto id){
                return w.world().npc_records().count(id)!=0;});
            re::CheckpointStore region(db,"region-home");
            auto rev=region.load();
            check(rev && rev->revision==2 && w.generation()==expected_generation,
                "reopen lost region revision or canonical world generation");
            const auto a=reconcile_medical_society(w,family,db,"settlement-home",
                home,region,"home",rev->revision,w.generation());
            check(a.reconciled && a.living_named==19 && a.medically_unavailable==2,
                "reopened family/workforce medical projection diverged");
            // Reconciliation never fabricates a medically fit staff member.
            family.record_death(10002,2,home,3,2,
                [](auto,auto,auto){return true;});
            denied([&]{(void)reconcile_medical_society(w,family,db,"settlement-home",
                home,region,"home",rev->revision,w.generation());},
                "unearned family death accepted without canonical casualty");
        }
        // The deliberate contradictory family fixture above must not be
        // presented as a recoverable integrated checkpoint.
        std::filesystem::remove(db);
        std::filesystem::remove(copy);
        std::cout<<"PASS q6 real ART02 family + regional checkpoint + v39 EMS workforce readiness, restart and fail-closed divergence\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL q6 society reconciliation: "<<e.what()<<'\n';
        std::filesystem::remove(db);std::filesystem::remove(copy);
        return 1;
    }
}
