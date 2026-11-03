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
        c.action==Action::HostTriageRescued || c.action==Action::HostTreatInjuries;
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
    const auto base=(std::filesystem::temp_directory_path()/"sg_q4_clinical").string();
    const auto db=base+".db",recovered=base+".recover";
    std::filesystem::remove(db);std::filesystem::remove(recovered);
    try{
        EntityId home=0,ferry=0,ems=0,clinic=0;
        std::string canonical;
        {
            Simulation s(3802);
            home=s.add_colony_ship(7,{0,0},200,20);
            ferry=s.add_colony_ship(7,{0,0},100,4);
            ems=s.add_colony_ship(7,{0,0},100,8);
            s.provision_colony_genesis(home,1000,3000,1000,1000,1000);
            const auto rock=s.add_asteroid({2000,0},90);
            s.enable_individual_workforce();s.upgrade_assignment_snapshot_format();
            PersistentWorld w(db,std::move(s));
            // Real ore -> manufactured modules -> player-built outpost, habitat and clinic.
            pc(w,1,home,Action::Mine,rock,80);
            pc(w,2,home,Action::RefineOre,0,80);
            pc(w,3,home,Action::FabricateModule,0,35);
            pc(w,4,home,Action::ConstructOutpost);
            ck(!w.world().built_outposts().empty(),"no construction site");
            clinic=w.world().built_outposts().begin()->first;
            pc(w,5,home,Action::ConstructUtility,clinic);
            pc(w,6,home,Action::ConstructHabitat,clinic);
            pc(w,7,home,Action::ConstructClinic,clinic);
            for(int selector=1;selector<=3;++selector)
                pc(w,10+selector,home,Action::TransferHabitatStock,clinic,140,selector);
            pc(w,20,home,Action::AssignHabitatResidents,clinic,2);
            pc(w,21,home,Action::AssignClinicStaff,clinic,2);
            pc(w,22,home,Action::TransferClinicStock,clinic,40,1);
            pc(w,23,home,Action::TransferClinicStock,clinic,40,2);
            ck(w.world().npc_records().at(1).location==clinic &&
               w.world().npc_records().at(2).location==clinic,"medics not physically stationed");
            // Untrained staff does not qualify as a named medical responder.
            ck(!w.submit_host_passenger_action(host(400,w.world().tick()+1,
                 Action::HostTrainMedic,clinic,5,0,{},3)),"ship pilot trained as resident medic");
            hc(w,401,Action::HostTrainMedic,clinic,1,0,{},3);
            hc(w,402,Action::HostTrainMedic,clinic,2,0,{},3);
            ck(w.world().named_medics().at(1).units==3 &&
               w.world().named_medics().at(2).units==3,"named medic training missing");
            ck(w.world().clinic(clinic)->water_kg==34 &&
               w.world().clinic(clinic)->substrate_kg==34,"training used no physical medicine");
            hc(w,500,Action::HostEmbarkPassengers,ferry,home,900,{5,6,7});
            hc(w,501,Action::HostResolveMixedFerryLoss,ferry,ems,900,{5,6,21,7,22,23,24},3);
            ck(w.world().voyage_status(900)==6 && w.world().voyage_status(501)==1,
               "wreck and EMS voyage not recorded");
            ck(w.world().find(ferry)->type==EntityType::Wreck &&
               w.world().colony_ship(ferry)->crew==0,"wreck still crewed");
            ck(w.world().colony_ship(ems)->crew==9,"surviving native crew not rehomed");
            ck(w.world().colony_ship(home)->crew==17,"home colony population not reduced");
            ck(w.world().npc_casualties().size()==4 && !w.world().npc_records().count(7),
               "living/dead accounting wrong");
            const auto& rehome=w.world().crew_rehomes().at(21);
            ck(rehome.wreck==ferry && rehome.rescue_ship==ems &&
               rehome.incident_command==501,"rescued crew provenance missing");
            const auto& aboard=w.world().npc_records().at(21);
            ck(aboard.home_ship==ems && aboard.assigned_site==ems &&
               aboard.transport_id==501 && aboard.location==ems,
               "surviving crew custody/assignment inconsistent");
            hc(w,502,Action::HostTriageRescued,ems,ems,501,{5,6,21},2);
            ck(w.world().medical_cases().at(21).severity==2 &&
               !w.world().medical_cases().at(21).treatment_tick,
               "injured living NPC not triaged");
            ck(!w.world().medical_cases().count(7),"dead NPC masquerading as patient");
            ck(w.world().npc_health_status(7)==NpcHealthStatus::Deceased &&
               w.world().npc_health_status(21)==NpcHealthStatus::Injured &&
               w.world().npc_health_status(6)==NpcHealthStatus::Injured,
               "deceased/injured status conflated");
            // Injury remains durable before any treatment is performed.
            const auto injured_state=w.world().canonical_state();
            ck(Simulation::from_snapshot(injured_state).npc_health_status(21)==
               NpcHealthStatus::Injured,"untreated injury lost in snapshot");
            {PersistentWorld look(db,Simulation(9));
                ck(look.world().canonical_state()==injured_state &&
                   look.world().npc_health_status(21)==NpcHealthStatus::Injured,
                   "untreated patient disappeared on DB restart");}
            // Test failed durable commit: journal and supplies remain unchanged.
            const auto treatment=host(503,w.world().tick()+1,Action::HostTreatInjuries,
                                      ems,clinic,1,{5,21});
            const auto before=w.world().canonical_state();
            bool failed=false;
            try{w.submit_host_medical_action(treatment,FailPoint::AfterReceiptWrite);}
            catch(const std::exception&){failed=true;}
            ck(failed && w.world().canonical_state()==before,"failed medical commit mutated patient/stock");
            ck(w.submit_host_medical_action(treatment),"treatment retry not accepted");
            const auto excess=host(505,treatment.execute_tick,Action::HostTreatInjuries,
                                   ems,clinic,1,{6});
            ck(w.submit_host_medical_action(excess),"concurrent EMS request not accepted");
            const auto queued=w.world().canonical_state();
            ck(Simulation::from_snapshot(queued).canonical_state()==queued,
               "pending medical commands failed snapshot round-trip");
            {PersistentWorld look(db,Simulation(9));
                ck(look.world().canonical_state()==queued,
                   "pending medical treatment missing after SQLite restart");}
            w.step();
            bool treated=false,full=false;
            for(const auto& e:w.world().events()){
                if(e.command_id==503)treated=e.result==Outcome::Applied;
                if(e.command_id==505)full=e.result==Outcome::NoResources;
            }
            ck(treated && full,"per-tick EMS treatment capacity not enforced");
            ck(w.world().npc_health_status(6)==NpcHealthStatus::Injured,
               "failed over-capacity treatment altered patient's status");
            ck(!w.submit_host_medical_action(treatment),"duplicated medical treatment accepted");
            ck(w.world().medical_cases().at(21).treatment_command==503 &&
               w.world().medical_cases().at(5).treatment_command==503,
               "medical treatments not recorded");
            ck(w.world().npc_health_status(21)==NpcHealthStatus::Treated &&
               w.world().npc_health_status(7)==NpcHealthStatus::Deceased,
               "care altered irreversible death or lost treatment state");
            ck(w.world().clinic(clinic)->ems_treated_cases==2 &&
               w.world().clinic(clinic)->ems_treatment_units==4 &&
               w.world().clinic(clinic)->water_kg==30 &&
               w.world().clinic(clinic)->substrate_kg==30,
               "EMS case counting or physical consumption wrong");
            hc(w,506,Action::HostTreatInjuries,ems,clinic,1,{6});
            ck(w.world().medical_cases().at(6).treatment_command==506 &&
               w.world().clinic(clinic)->ems_treated_cases==3 &&
               w.world().clinic(clinic)->ems_treatment_units==6 &&
               w.world().clinic(clinic)->water_kg==28,
               "next-tick EMS capacity failed or consumed phantom supplies");
            ck(!w.submit_host_passenger_action(host(504,w.world().tick()+1,
                Action::HostTriageRescued,ems,ems,501,{7},2)),"deceased patient triaged");
            canonical=w.world().canonical_state();
            ck(Simulation::from_snapshot(canonical).canonical_state()==canonical,"v38 snapshot fails round trip");
            bool rejected=false;
            try{(void)Simulation::from_snapshot(corrupt_first(canonical,
                 "patient|21|501|502|","patient|21|501|99999|"));}
            catch(const std::exception&){rejected=true;}
            ck(rejected,"forged patient receipt accepted");
            rejected=false;
            try{(void)Simulation::from_snapshot(corrupt_first(canonical,
                 "crewrehome|21|","crewrehome|9999|"));}
            catch(const std::exception&){rejected=true;}
            ck(rejected,"forged crew rescue accepted");
        }
        {
            PersistentWorld reopened(db,Simulation(1));
            ck(reopened.world().canonical_state()==canonical,"v38 database restart diverged");
        }
        PersistentWorld::recover_copy(db,recovered);
        {
            PersistentWorld check(recovered,Simulation(1));
            ck(check.world().canonical_state()==canonical,"independent genesis replay diverged");
        }
        std::filesystem::remove(db);std::filesystem::remove(recovered);
        std::cout<<"PASS v38 real built clinic, qualified named medics, stocked EMS treatment, crew rehome, patients/deaths, restart/replay, commit failure and corrupt state refusal\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL q4 medical care: "<<e.what()<<'\n';
        std::filesystem::remove(db);std::filesystem::remove(recovered);
        return 1;
    }
}
