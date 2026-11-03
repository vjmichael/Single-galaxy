#include "galaxy/Persistence.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace galaxy;
static void ck(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
static Command player(std::uint64_t id,std::uint64_t tick,EntityId ship,Action a,
                       EntityId site=0,std::int64_t amount=1,int selector=0){
    Command c{};c.id=id;c.sequence=id;c.issuer=7;c.execute_tick=tick;
    c.actor=ship;c.target=site;c.action=a;c.amount=amount;c.thrust_x=selector;
    return c;
}
static Command host(std::uint64_t id,std::uint64_t tick,Action a,EntityId actor,
                    EntityId target,std::uint64_t transport=0,
                    std::vector<std::uint64_t> ids={},std::int64_t amount=0){
    Command c{};c.id=id;c.sequence=id;c.issuer=Simulation::kServerAuthority;
    c.execute_tick=tick;c.action=a;c.actor=actor;c.target=target;
    c.transport_id=transport;c.passenger_ids=std::move(ids);c.amount=amount;return c;
}
static void send(PersistentWorld& w,Command c,bool hosted=false){
    const bool medical=c.action==Action::HostTrainMedic ||
        c.action==Action::HostTriageRescued || c.action==Action::HostTreatInjuries ||
        c.action==Action::HostScheduleMedicShift || c.action==Action::HostLoadMedicalKit ||
        c.action==Action::HostMobileStabilize;
    const bool ok=!hosted?w.submit(c):medical?
        w.submit_host_medical_action(c):w.submit_host_passenger_action(c);
    ck(ok,"submit denied command "+std::to_string(c.id));w.step();
    ck(w.world().events().back().result==Outcome::Applied,
       "execution failed command "+std::to_string(c.id)+" "+to_string(w.world().events().back().result));
}
static void pc(PersistentWorld& w,std::uint64_t id,EntityId ship,Action action,
               EntityId target=0,std::int64_t amount=1,int selector=0){
    send(w,player(id,w.world().tick()+1,ship,action,target,amount,selector));
}
static void hc(PersistentWorld& w,std::uint64_t id,Action action,EntityId actor,
               EntityId target,std::uint64_t transport=0,
               std::vector<std::uint64_t> ids={},std::int64_t amount=0){
    send(w,host(id,w.world().tick()+1,action,actor,target,transport,std::move(ids),amount),true);
}
static std::string corrupt_first(const std::string& original,const std::string& needle,const std::string& change){
    auto s=original;const auto i=s.find(needle);ck(i!=std::string::npos,"snapshot missing "+needle);
    s.replace(i,needle.size(),change);return s;
}
int main(){
    const auto base=(std::filesystem::temp_directory_path()/"sg_q5_recovery").string();
    const auto db=base+".db",recovered=base+".recover";
    std::filesystem::remove(db);std::filesystem::remove(recovered);
    try{
        EntityId home=0,ferry=0,ems=0,clinic=0;
        std::string canonical;
        {
            Simulation s(3905);
            home=s.add_colony_ship(7,{0,0},200,20);
            ferry=s.add_colony_ship(7,{0,0},100,4);
            ems=s.add_colony_ship(7,{0,0},100,8);
            s.provision_colony_genesis(home,1000,3000,1000,1000,1000);
            const auto rock=s.add_asteroid({2000,0},90);
            s.enable_individual_workforce();s.upgrade_assignment_snapshot_format();
            PersistentWorld w(db,std::move(s));
            pc(w,1,home,Action::Mine,rock,80);
            pc(w,2,home,Action::RefineOre,0,80);
            pc(w,3,home,Action::FabricateModule,0,35);
            pc(w,4,home,Action::ConstructOutpost);
            clinic=w.world().built_outposts().begin()->first;
            pc(w,5,home,Action::ConstructUtility,clinic);
            pc(w,6,home,Action::ConstructHabitat,clinic);
            pc(w,7,home,Action::ConstructClinic,clinic);
            for(int selector=1;selector<=3;++selector)
                pc(w,10+selector,home,Action::TransferHabitatStock,clinic,140,selector);
            pc(w,20,home,Action::AssignHabitatResidents,clinic,2);
            pc(w,21,home,Action::AssignClinicStaff,clinic,2);
            pc(w,22,home,Action::TransferClinicStock,clinic,70,1);
            pc(w,23,home,Action::TransferClinicStock,clinic,70,2);
            // Clinic qualification remains local and consumes real raw supplies.
            hc(w,401,Action::HostTrainMedic,clinic,1,0,{},3);
            hc(w,402,Action::HostTrainMedic,clinic,2,0,{},3);
            hc(w,403,Action::HostScheduleMedicShift,clinic,1,0,{},200);
            hc(w,404,Action::HostScheduleMedicShift,clinic,2,0,{},200);
            // Mobile kit physically moves 10 kg of each input from clinic to EMS.
            const auto initial_mass=w.world().total_material_kg();
            hc(w,405,Action::HostLoadMedicalKit,ems,clinic,0,{},10);
            ck(w.world().mobile_medical_kits().at(ems).water_kg==10 &&
               w.world().clinic(clinic)->water_kg==54 &&
               w.world().total_material_kg()==initial_mass,
               "mobile medical stock duplicated or disappeared");
            // New in v39: pre-trained native ship crew can respond in the mobile EMS.
            hc(w,406,Action::HostTrainMedic,clinic,25,0,{},3);
            ck(w.world().named_medics().at(25).units==3 &&
               w.world().npc_records().at(25).location==ems,
               "mobile medic was not real trained ship crew");
            hc(w,500,Action::HostEmbarkPassengers,ferry,home,900,{5,6,7});
            hc(w,501,Action::HostResolveMixedFerryLoss,ferry,ems,900,{5,6,21,7,22,23,24},3);
            hc(w,502,Action::HostTriageRescued,ems,ems,501,{5,6,21},2);
            ck(w.world().npc_health_status(5)==NpcHealthStatus::Injured &&
               !w.world().npc_available_for_duty(5),
               "first injury not recorded or available for work");
            hc(w,503,Action::HostMobileStabilize,ems,25,25,{5,6});
            ck(w.world().npc_health_status(5)==NpcHealthStatus::Stabilized &&
               w.world().mobile_medical_kits().at(ems).water_kg==6 &&
               w.world().mobile_medical_kits().at(ems).medical_waste_kg==4,
               "mobile stabilization used phantom materials");
            ck(!w.submit_host_medical_action(host(509,w.world().tick()+1,
                Action::HostMobileStabilize,ems,25,25,{5})),
               "repeat mobile stabilization was accepted");
            // Failed commit must not partially heal a patient or burn inventory.
            const auto treatment=host(504,w.world().tick()+1,
                 Action::HostTreatInjuries,ems,clinic,1,{5,6});
            const auto before=w.world().canonical_state();
            bool failed=false;
            try{w.submit_host_medical_action(treatment,FailPoint::AfterReceiptWrite);}
            catch(const std::exception&){failed=true;}
            ck(failed && w.world().canonical_state()==before,
               "failed treatment commit leaked state");
            ck(w.submit_host_medical_action(treatment),"treatment retry refused");
            w.step();
            ck(w.world().npc_health_status(5)==NpcHealthStatus::Recovering &&
               w.world().medical_cases().at(5).recovery_due_tick==w.world().tick()+6,
               "recovery timer not committed with treatment");
            const auto recovering=w.world().canonical_state();
            ck(Simulation::from_snapshot(recovering).canonical_state()==recovering,
               "active recovery snapshot failed reconstruction");
            { PersistentWorld reopen(db,Simulation(1));
              ck(reopen.world().canonical_state()==recovering,
                 "recovery clock did not survive database restart"); }
            w.steps(5);
            ck(w.world().npc_health_status(5)==NpcHealthStatus::Recovering,
               "recovery finished one tick early");
            w.step();
            ck(w.world().npc_health_status(5)==NpcHealthStatus::Recovered &&
               w.world().medical_cases().at(5).recovered_tick==w.world().tick(),
               "recovery did not finish deterministically");
            // A new injury is separately triaged; previous episode must not vanish.
            hc(w,510,Action::HostTriageRescued,ems,ems,501,{5},1);
            ck(w.world().medical_history().size()==1 &&
               w.world().medical_history().at({5,502}).recovered_tick!=0 &&
               w.world().medical_cases().at(5).triage_command==510 &&
               w.world().npc_health_status(5)==NpcHealthStatus::Injured,
               "repeat injury replaced permanent episode history");
            hc(w,511,Action::HostTreatInjuries,ems,clinic,1,{5});
            ck(w.world().npc_health_status(5)==NpcHealthStatus::Recovering,
               "second injury did not enter recovery");
            w.steps(3);
            ck(w.world().npc_health_status(5)==NpcHealthStatus::Recovered &&
               w.world().medical_cases().at(5).recovered_tick &&
               w.world().clinic(clinic)->ems_treated_cases==3 &&
               w.world().clinic(clinic)->ems_treatment_units==5,
               "episode accounting or second recovery failed");
            ck(w.world().npc_health_status(7)==NpcHealthStatus::Deceased &&
               w.world().npc_casualties().count(7) &&
               w.world().crew_rehomes().count(21),
               "ferry casualty and crew custody were altered by medicine");
            // A clinic with no staff on duty cannot treat the next injury.
            w.steps(200); // shifts 403/404 are now both expired
            hc(w,520,Action::HostTriageRescued,ems,ems,501,{5},1);
            const auto clinic_stock_before=w.world().clinic(clinic)->water_kg;
            const auto without_staff=host(521,w.world().tick()+1,
                Action::HostTreatInjuries,ems,clinic,1,{5});
            ck(w.submit_host_medical_action(without_staff),"expiry check command not admitted");
            w.step();
            ck(w.world().events().back().command_id==521 &&
               w.world().events().back().result==Outcome::NoResources &&
               w.world().npc_health_status(5)==NpcHealthStatus::Injured &&
               w.world().clinic(clinic)->water_kg==clinic_stock_before,
               "expired staffing shift treated a patient or consumed supplies");
            hc(w,522,Action::HostScheduleMedicShift,clinic,1,0,{},10);
            hc(w,523,Action::HostTreatInjuries,ems,clinic,1,{5});
            w.steps(3);
            ck(w.world().npc_health_status(5)==NpcHealthStatus::Recovered &&
               w.world().medical_history().size()==2 &&
               w.world().clinic(clinic)->ems_treated_cases==4,
               "new shift did not complete recurring injury with retained history");
            const auto audit=w.world().assess_medical_population();
            ck(audit.reconciled && audit.by_home_ship.at(home).deceased==1 &&
               audit.by_home_ship.at(home).living==audit.by_home_ship.at(home).on_duty+
                   audit.by_home_ship.at(home).unavailable &&
               audit.by_home_ship.at(home).in_transit>=2 &&
               audit.by_home_ship.at(home).fit+audit.by_home_ship.at(home).injured+
               audit.by_home_ship.at(home).stabilized+
               audit.by_home_ship.at(home).recovering==audit.by_home_ship.at(home).living,
               "family/workforce home population audit inconsistent");
            canonical=w.world().canonical_state();
            ck(Simulation::from_snapshot(canonical).canonical_state()==canonical,
               "v39 snapshot did not reconstruct");
            // Reject forged history, shift receipts and ship kit ownership.
            bool bad=false;
            try{(void)Simulation::from_snapshot(corrupt_first(canonical,
                "patienthistory|5|501|502|","patienthistory|5|501|999|"));}
            catch(const std::exception&){bad=true;}
            ck(bad,"forged historical injury episode accepted");
            bad=false;
            try{(void)Simulation::from_snapshot(corrupt_first(canonical,
                "medicshift|403|","medicshift|999999|"));}
            catch(const std::exception&){bad=true;}
            ck(bad,"forged medic shift receipt accepted");
            bad=false;
            try{(void)Simulation::from_snapshot(corrupt_first(canonical,
                "medicalkit|"+std::to_string(ems)+"|","medicalkit|999999|"));}
            catch(const std::exception&){bad=true;}
            ck(bad,"kit on nonexistent ship accepted");
        }
        {PersistentWorld reopened(db,Simulation(1));
         ck(reopened.world().canonical_state()==canonical,"v39 restart diverged");}
        PersistentWorld::recover_copy(db,recovered);
        {PersistentWorld replay(recovered,Simulation(1));
         ck(replay.world().canonical_state()==canonical,"v39 genesis replay diverged");}
        std::filesystem::remove(db);std::filesystem::remove(recovered);
        std::cout<<"PASS v39 durable episodes, recovery clock, clinic shifts, finite medkit, stabilization, real EMS, population audit, crash safety and genesis replay\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL q5 medical recovery: "<<e.what()<<'\n';
        std::filesystem::remove(db);std::filesystem::remove(recovered);
        return 1;
    }
}
