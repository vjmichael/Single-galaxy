#include "galaxy/Persistence.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace galaxy;
static void ck(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static Command host(std::uint64_t id,std::uint64_t tick,Action act,EntityId ferry,EntityId target,
                    std::uint64_t voyage,std::vector<std::uint64_t> people,std::int64_t count=0){
    Command c{};c.id=id;c.execute_tick=tick;c.issuer=Simulation::kServerAuthority;
    c.actor=ferry;c.target=target;c.action=act;c.transport_id=voyage;
    c.passenger_ids=std::move(people);c.amount=count;return c;
}
struct Fixture{EntityId home=0,ferry=0,rescue=0;};
static Fixture boot(const std::string& path,std::int64_t rescue_x=0){
    std::filesystem::remove(path);
    Simulation sim(734);
    Fixture f;
    f.home=sim.add_colony_ship(7,{0,0},100,20);
    f.ferry=sim.add_colony_ship(7,{0,0},100,6);
    f.rescue=sim.add_colony_ship(7,{rescue_x,0},100,8);
    sim.enable_individual_workforce();sim.upgrade_assignment_snapshot_format();
    PersistentWorld w(path,std::move(sim));
    ck(w.submit_host_passenger_action(host(600,1,Action::HostEmbarkPassengers,f.ferry,f.home,900,{1,2,3})),"board passenger manifest");
    w.step();
    return f;
}
static Command disaster(const Fixture& f,std::uint64_t id=701){
    // 2 survivors, followed by all seven declared fatalities, including 6 ferry crew.
    return host(id,2,Action::HostResolveMixedFerryLoss,f.ferry,f.rescue,900,
        {1,2,3,21,22,23,24,25,26},2);
}
static void mixed_case(const std::string& path){
    const auto f=boot(path);
    std::string stable;
    {
        PersistentWorld w(path,Simulation(1));
        const auto bob_before=w.world().npc_records().at(1);
        auto incident=disaster(f);
        ck(w.submit_host_passenger_action(incident),"EMS dispatch refused");
        ck(!w.submit_host_passenger_action(incident),"duplicate dispatch accepted");
        w.step();
        ck(w.world().events().back().result==Outcome::Applied,"mixed incident not applied");
        ck(w.world().voyage_status(900)==6 && w.world().voyage_status(701)==1,"lost + rescue voyage missing");
        ck(w.world().find(f.ferry)->type==EntityType::Wreck,"destroyed ferry is not a wreck");
        ck(w.world().colony_ship(f.ferry)->crew==0,"wreck still staffed");
        ck(w.world().colony_ship(f.home)->crew==19,"home colony casualty accounting wrong");
        ck(w.world().npc_casualties().size()==7,"mixed outcome casualty count wrong");
        const auto ems=w.world().ferry_emergency_report(900);
        ck(ems.recorded && ems.incident_command_id==701 && ems.wreck==f.ferry &&
           ems.rescue_ship==f.rescue && ems.rescue_voyage_id==701 &&
           ems.survivors==std::vector<std::uint64_t>({1,2}) &&
           ems.deceased==std::vector<std::uint64_t>({3,21,22,23,24,25,26}),
           "derived EMS incident report incomplete");
        for(auto dead:std::vector<std::uint64_t>{3,21,22,23,24,25,26}){
            ck(!w.world().npc_records().count(dead),"dead NPC still living");
            const auto& tomb=w.world().npc_casualties().at(dead);
            ck(tomb.command_id==701 && tomb.ferry==f.ferry,"death provenance wrong");
        }
        for(auto survivor:std::vector<std::uint64_t>{1,2}){
            const auto& n=w.world().npc_records().at(survivor);
            ck(n.location==f.rescue && n.transport_id==701,"survivor not aboard EMS ship");
            ck(n.home_ship==f.home && n.assigned_site==f.home && n.role==NpcRole::Ship,"survivor assignment was altered");
        }
        const auto& bob=w.world().npc_records().at(1);
        ck(bob.id==bob_before.id && bob.mentor_id==bob_before.mentor_id &&
           bob.farm_xp==bob_before.farm_xp,"Bob lost immutable identity/skill record");
        ck(!w.submit_host_passenger_action(disaster(f,702)),"ferry incident reused terminal voyage");
        stable=w.world().canonical_state();
    }
    {
        PersistentWorld reopened(path,Simulation(8));
        ck(reopened.world().canonical_state()==stable,"crash restart changed mixed state");
        ck(reopened.submit_host_passenger_action(host(703,3,Action::HostDisembarkPassengers,
                f.rescue,f.home,701,{1,2})),"EMS ship could not discharge survivors");
        reopened.step();
        ck(reopened.world().voyage_status(701)==2,"survivors not discharged");
        ck(reopened.world().npc_records().at(1).transport_id==0,"Bob still marked in rescue leg");
        stable=reopened.world().canonical_state();
    }
    const auto replay=path+".replay";std::filesystem::remove(replay);
    PersistentWorld::recover_copy(path,replay);
    {
        PersistentWorld a(path,Simulation(1)),b(replay,Simulation(2));
        ck(a.world().canonical_state()==b.world().canonical_state() &&
           a.world().canonical_state()==stable,"genesis replay diverged after EMS discharge");
    }
    std::filesystem::remove(path);std::filesystem::remove(replay);
}
static void refusals(const std::string& path){
    auto f=boot(path);
    {
        PersistentWorld w(path,Simulation(1));
        auto incident=disaster(f);
        auto bad=incident;bad.issuer=7;
        ck(!w.submit_host_passenger_action(bad),"non-host EMS permitted");
        bad=incident;bad.amount=0;ck(!w.submit_host_passenger_action(bad),"zero survivors accepted");
        bad=incident;bad.amount=9;ck(!w.submit_host_passenger_action(bad),"no fatalities accepted");
        bad=incident;bad.passenger_ids.pop_back();ck(!w.submit_host_passenger_action(bad),"missing crew accepted");
        bad=incident;bad.passenger_ids={1,2,3,21,22,23,24,25,25};
        ck(!w.submit_host_passenger_action(bad),"duplicate casualty accepted");
        bad=incident;bad.passenger_ids={2,1,3,21,22,23,24,25,26};
        ck(!w.submit_host_passenger_action(bad),"unsorted survivor segment accepted");
        bad=incident;bad.passenger_ids={21,2,1,3,22,23,24,25,26};bad.amount=2;
        ck(!w.submit_host_passenger_action(bad),"native crew rescued without rehoming");
        bad=incident;bad.transport_id=701;ck(!w.submit_host_passenger_action(bad),"rescue id aliases source voyage");
        ck(w.world().voyage_status(900)==1 && w.world().npc_casualties().empty(),"rejected dispatch mutated world");
    }
    std::filesystem::remove(path);
    const auto far=path+"_distant";
    f=boot(far,10000);
    {
        PersistentWorld w(far,Simulation(1));
        ck(!w.submit_host_passenger_action(disaster(f)),"distant EMS ferry accepted");
    }
    std::filesystem::remove(far);
}
static void capacity(const std::string& path){
    auto f=boot(path);
    {
        PersistentWorld w(path,Simulation(1));
        std::vector<std::uint64_t> occupiers;
        for(std::uint64_t id=4;id<=19;++id)occupiers.push_back(id);
        ck(w.submit_host_passenger_action(host(650,2,Action::HostEmbarkPassengers,
            f.rescue,f.home,910,occupiers)),"fill EMS berths");
        w.step();
        auto incident=disaster(f,701);incident.execute_tick=3;
        ck(!w.submit_host_passenger_action(incident),"overfull EMS craft accepted");
        ck(w.world().npc_records().at(1).location==f.ferry,"overbooking moved Bob");
    }
    std::filesystem::remove(path);
}
static void pending_rescue_capacity(const std::string& path){
    std::filesystem::remove(path);
    Simulation sim(8);
    const auto home=sim.add_colony_ship(7,{0,0},100,40);
    const auto ferry_a=sim.add_colony_ship(7,{0,0},100,6);
    const auto ferry_b=sim.add_colony_ship(7,{0,0},100,6);
    const auto ems=sim.add_colony_ship(7,{0,0},100,8);
    sim.enable_individual_workforce();sim.upgrade_assignment_snapshot_format();
    std::vector<std::uint64_t> first,second;
    for(std::uint64_t i=1;i<=9;++i)first.push_back(i);
    for(std::uint64_t i=10;i<=18;++i)second.push_back(i);
    {
        PersistentWorld w(path,std::move(sim));
        ck(w.submit_host_passenger_action(host(600,1,Action::HostEmbarkPassengers,
             ferry_a,home,900,first)),"first passenger ferry board");
        ck(w.submit_host_passenger_action(host(601,1,Action::HostEmbarkPassengers,
             ferry_b,home,901,second)),"second passenger ferry board");
        w.step();
        auto a=first,b=second;
        for(std::uint64_t id=41;id<=46;++id)a.push_back(id);
        for(std::uint64_t id=47;id<=52;++id)b.push_back(id);
        ck(w.submit_host_passenger_action(host(701,2,Action::HostResolveMixedFerryLoss,
              ferry_a,ems,900,a,9)),"first EMS dispatch");
        ck(!w.submit_host_passenger_action(host(702,2,Action::HostResolveMixedFerryLoss,
              ferry_b,ems,901,b,9)),"two pending EMS jobs overbooked 16 berths");
        w.step();
        ck(w.world().voyage_status(900)==6 && w.world().voyage_status(901)==1,
           "pending EMS conflict mutated second ferry");
    }
    std::filesystem::remove(path);
}
int main(){
    const auto root=(std::filesystem::temp_directory_path()/"sg_demog05q3_ems").string();
    mixed_case(root+"_mixed.db");
    refusals(root+"_refuse.db");
    capacity(root+"_capacity.db");
    pending_rescue_capacity(root+"_pending_capacity.db");
    std::cout<<"PASS v37 atomic two-survivor, seven-fatality incident, EMS voyage, discharge, replay, capacity and refusal\n";
}
