#include "galaxy/Persistence.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace galaxy;
static void chk(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
static Command host(std::uint64_t id,std::uint64_t tick,Action action,
       EntityId ferry,EntityId to,std::uint64_t voyage,std::vector<std::uint64_t> ids,
       std::int64_t amount=0,std::uint64_t sequence=0){
    Command c{};c.id=id;c.execute_tick=tick;c.sequence=sequence;c.issuer=Simulation::kServerAuthority;
    c.action=action;c.actor=ferry;c.target=to;c.transport_id=voyage;
    c.passenger_ids=std::move(ids);c.amount=amount;return c;
}
static std::vector<std::uint64_t> full_manifest(){return {2,21,22,23,24,25,26};}
struct Fixture {EntityId home,ferry,rescue;};
static Fixture world(const std::string& db){
    std::filesystem::remove(db);
    Simulation s(43);
    Fixture f{s.add_colony_ship(7,{0,0},100,20),0,0};
    f.ferry=s.add_colony_ship(7,{0,0},100,6);
    f.rescue=s.add_colony_ship(7,{0,0},100,6);
    s.enable_individual_workforce();s.upgrade_assignment_snapshot_format();
    PersistentWorld w(db,std::move(s));
    chk(w.submit_host_passenger_action(host(700,1,Action::HostEmbarkPassengers,f.ferry,f.home,900,{1})),"board bob");
    chk(w.submit_host_passenger_action(host(701,1,Action::HostEmbarkPassengers,f.ferry,f.home,901,{2})),"board alice");
    w.step();
    chk(w.submit_host_passenger_action(host(702,2,Action::HostEvacuatePassengers,f.ferry,f.rescue,900,{1},902)),"rescue bob");
    w.step();
    chk(w.world().npc_records().at(1).location==f.rescue && w.world().npc_records().at(1).transport_id==902,"Bob safe");
    return f;
}
static void main_case(const std::string& db){
    auto f=world(db);
    const auto recovered=db+".replay";std::filesystem::remove(recovered);
    std::string canonical;
    {
        PersistentWorld w(db,Simulation(1));
        auto wrong=host(703,3,Action::HostDestroyPassengerFerry,f.ferry,f.ferry,901,{2});
        chk(!w.submit_host_passenger_action(wrong),"incomplete fatal manifest allowed");
        wrong.passenger_ids=full_manifest();wrong.issuer=7;
        chk(!w.submit_host_passenger_action(wrong),"unprivileged scuttle accepted");
        wrong.issuer=Simulation::kServerAuthority;wrong.passenger_ids={2,21,22,23,24,25,25};
        chk(!w.submit_host_passenger_action(wrong),"duplicate casualty accepted");
        wrong.passenger_ids=full_manifest();wrong.amount=1;
        chk(!w.submit_host_passenger_action(wrong),"unproven outcome accepted");
        wrong.amount=0;
        chk(w.submit_host_passenger_action(wrong),"scuttle command rejected");
        chk(!w.submit_host_passenger_action(wrong),"duplicate scuttle accepted");
        w.step();
        chk(w.world().events().back().result==Outcome::Applied,"scuttle did not apply");
        chk(w.world().voyage_status(901)==6,"lost terminal voyage");
        chk(w.world().find(f.ferry)->type==EntityType::Wreck,"no physical wreck");
        chk(w.world().npc_casualties().size()==7,"fatality conservation");
        for(auto id:full_manifest()){
            chk(!w.world().npc_records().count(id),"dead NPC still living");
            const auto& d=w.world().npc_casualties().at(id);
            chk(d.identity.id==id && d.identity.location==f.ferry && d.command_id==703 && d.ferry==f.ferry,"death provenance lost");
            chk(d.identity.transport_id==(id==2?901:0),"native crew vs passenger provenance");
        }
        chk(w.world().npc_records().at(1).location==f.rescue,"Bob killed by database event");
        chk(w.world().npc_records().at(1).transport_id==902,"Bob rescue voyage lost");
        chk(!w.submit_host_passenger_action(host(704,4,Action::HostDestroyPassengerFerry,f.ferry,f.ferry,901,full_manifest())),"lost voyage reused");
        canonical=w.world().canonical_state();
    }
    {
        PersistentWorld reopen(db,Simulation(99));
        chk(reopen.world().canonical_state()==canonical,"restart altered casualty state");
        reopen.steps(20);
        chk(reopen.world().npc_casualties().size()==7,"time passage respawned NPC");
        chk(reopen.world().npc_records().at(1).location==f.rescue,"Bob's rescue was not durable");
    }
    PersistentWorld::recover_copy(db,recovered);
    {
        PersistentWorld direct(db,Simulation(3)),replay(recovered,Simulation(5));
        chk(direct.world().canonical_state()==replay.world().canonical_state(),"independent genesis journal replay diverged");
    }
    std::filesystem::remove(db);std::filesystem::remove(recovered);
}
static void race(const std::string& db, bool destroy_first){
    auto f=world(db);
    const auto t=3;
    const auto destroy=host(810,t,Action::HostDestroyPassengerFerry,f.ferry,f.ferry,901,full_manifest(),0,destroy_first?1:2);
    const auto rescue=host(811,t,Action::HostEvacuatePassengers,f.ferry,f.rescue,901,{2},903, destroy_first?2:1);
    {
        PersistentWorld w(db,Simulation(1));
        chk(w.submit_host_passenger_action(destroy),"queue competing destruction");
        chk(w.submit_host_passenger_action(rescue),"queue competing rescue");
        w.step();
        auto ev=w.world().events();
        chk(ev.size()>=2,"missing sequence outcomes");
        const auto& last=ev.back(),&prev=ev.at(ev.size()-2);
        chk((prev.result==Outcome::Applied && last.result!=Outcome::Applied) ||
            (last.result==Outcome::Applied && prev.result!=Outcome::Applied),"same-tick double commit or double refusal");
        chk(w.world().voyage_status(901)==(destroy_first?6U:5U),"command order not deterministic");
        chk(w.world().npc_records().count(2)==(destroy_first?0U:1U),"Bob's friend duplicated or killed unexpectedly");
    }
    const auto replay=db+".replay";std::filesystem::remove(replay);
    PersistentWorld::recover_copy(db,replay);
    {PersistentWorld a(db,Simulation(1)),b(replay,Simulation(1));
        chk(a.world().canonical_state()==b.world().canonical_state(),"race journal replay differs");}
    std::filesystem::remove(db);std::filesystem::remove(replay);
}
int main(){
    const auto base=(std::filesystem::temp_directory_path()/"sg_demog05q2_v36").string();
    main_case(base+"_fatal.db");
    race(base+"_race_loss.db",true);
    race(base+"_race_rescue.db",false);
    std::cout<<"PASS v36 explicit fatal manifest, 7 tombstones, preserved Bob, wreck, restart, genesis replay, competing incident order\\n";
}
