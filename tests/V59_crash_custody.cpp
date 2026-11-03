#include "galaxy/Persistence.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace galaxy;
namespace fs = std::filesystem;

static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
template<class F> static void refuses(F&& f, const char* why) {
    bool threw=false;
    try { f(); } catch (const std::exception&) { threw=true; }
    check(threw,why);
}
static Command command(std::uint64_t id, const PersistentWorld& w, EntityId actor,
                       EntityId target, Action a, bool host=false) {
    Command c{};
    c.id=id;
    c.sequence=id;
    c.execute_tick=w.world().tick()+1;
    c.issuer=host?Simulation::kServerAuthority:7;
    c.actor=actor;
    c.target=target;
    c.action=a;
    if(host){c.transport_id=900;c.passenger_ids={1};}
    return c;
}
static void applied(PersistentWorld& w, std::uint64_t id){
    w.step();
    check(!w.world().events().empty() && w.world().events().back().command_id==id &&
          w.world().events().back().result==Outcome::Applied,"scheduled voyage command did not apply");
}
static void bob(const PersistentWorld& w, EntityId home, EntityId expected,
                std::uint64_t voyage, const char* why){
    const auto& all=w.world().npc_records();
    auto it=all.find(1);
    check(it!=all.end() && it->second.id==1 && it->second.home_ship==home &&
          it->second.assigned_site==home && it->second.location==expected &&
          it->second.transport_id==voyage && w.world().npc_casualties().count(1)==0,why);
    std::size_t count=0;
    for(const auto& [id,n]:all)if(id==1 && n.location==expected)++count;
    check(count==1 && w.world().assess_medical_population().reconciled,
          "duplicate Bob or broken population after interrupted voyage");
}
int main(){
    const auto path=(fs::temp_directory_path()/"sg_q6d1_inflight_crash.db").string();
    const auto recovered=path+".replay";
    fs::remove(path);fs::remove(recovered);
    EntityId home=0,ferry=0,destination=0,gate=0;
    try {
        {
            Simulation s(20261102);
            s.add_system(2);
            home=s.add_colony_ship(7,{0,0},100,6);
            ferry=s.add_colony_ship(7,{0,0},100,6);
            destination=s.add_colony_ship(7,{0,0},100,6);
            gate=s.add_gate(1,{0,0},2,{5000,0});
            s.enable_individual_workforce();s.upgrade_assignment_snapshot_format();
            PersistentWorld w(path,std::move(s));
            // Destination goes to system 2 independently. Bob remains at origin.
            check(w.submit(command(100,w,destination,gate,Action::RequestJump)),
                  "queue destination positioning");
            applied(w,100);
            w.steps(Simulation::kJumpTransitTicks+Simulation::kArrivalCloakTicks+1);
            check(w.world().find(destination)->system_id==2,"destination never reached system 2");
            bob(w,home,home,0,"Bob should still be at origin");
            auto embark=command(101,w,ferry,home,Action::HostEmbarkPassengers,true);
            refuses([&]{(void)w.submit_host_passenger_action(embark,FailPoint::AfterReceiptWrite);},
                    "precommit failure in boarding did not fail");
            bob(w,home,home,0,"failed boarding submission moved Bob");
        }
        {
            PersistentWorld w(path,Simulation(99));
            bob(w,home,home,0,"restart after failed boarding must leave Bob at origin");
            check(w.submit_host_passenger_action(command(101,w,ferry,home,Action::HostEmbarkPassengers,true)),
                  "durable boarding submission rejected after retry");
            const auto before=w.world().canonical_state();
            const auto generation=w.generation();
            refuses([&]{w.step(FailPoint::AfterSnapshotWrite);},
                    "failed boarding execution was committed");
            check(w.generation()==generation && w.world().canonical_state()==before,
                  "failed boarding execution mutated published state");
        }
        {
            PersistentWorld w(path,Simulation(99));
            bob(w,home,home,0,"failed boarding step must recover previous committed home");
            applied(w,101);
            bob(w,home,ferry,900,"committed boarding failed physical custody");
            check(w.world().voyage_status(900)==1,"voyage not active after boarding");
            // A remote destination cannot accept a passenger before ferry arrives.
            check(!w.submit_host_passenger_action(command(102,w,ferry,destination,
                Action::HostDisembarkPassengers,true)),"remote disembark teleported Bob");
            // Queue jump; failure must leave Bob on committed ship at origin system.
            auto jump=command(103,w,ferry,gate,Action::RequestJump);
            refuses([&]{(void)w.submit(jump,FailPoint::AfterReceiptWrite);},
                    "failed jump submission committed");
            bob(w,home,ferry,900,"failed jump submission lost Bob");
        }
        {
            PersistentWorld w(path,Simulation(99));
            check(w.world().find(ferry)->system_id==1,"failed jump submission moved ferry");
            check(w.submit(command(103,w,ferry,gate,Action::RequestJump)),"jump retry rejected");
            const auto committed=w.world().canonical_state();
            refuses([&]{w.step(FailPoint::AfterSnapshotWrite);},
                    "jump failed-commit simulation should be interrupted");
            check(w.world().canonical_state()==committed,"failed jump leaked in-memory position");
        }
        {
            PersistentWorld w(path,Simulation(99));
            check(w.world().find(ferry)->system_id==1,"failed jump execution moved ship on restart");
            bob(w,home,ferry,900,"failed jump execution moved Bob");
            applied(w,103);
            bob(w,home,ferry,900,"jump start must preserve physical ferry custody");
            check(w.world().find(ferry)->jump_state==ShipJumpState::Jumping,
                  "ferry not in committed jumping state");
            const auto checkpoint=w.world().canonical_state();
            refuses([&]{w.step(FailPoint::AfterSnapshotWrite);},
                    "mid-flight failed progress commit not trapped");
            check(w.world().canonical_state()==checkpoint,
                  "failed mid-flight progress published despite SQLite rollback");
        }
        {
            PersistentWorld w(path,Simulation(99));
            bob(w,home,ferry,900,"mid-flight crash must restore Bob aboard ferry");
            check(w.world().find(ferry)->jump_state==ShipJumpState::Jumping,
                  "mid-flight crash lost committed flight state");
            // One tick of travel committed, then a crash on the following tick.
            w.step();
            const auto mid=w.world().canonical_state();
            refuses([&]{w.step(FailPoint::AfterSnapshotWrite);},
                    "mid-flight second failure not trapped");
            check(w.world().canonical_state()==mid,"rollback changed last committed travel progress");
        }
        {
            PersistentWorld w(path,Simulation(99));
            bob(w,home,ferry,900,"second mid-flight restart duplicated/moved Bob");
            w.steps(Simulation::kJumpTransitTicks+Simulation::kArrivalCloakTicks+1);
            check(w.world().find(ferry)->system_id==2 &&
                  w.world().find(ferry)->jump_state==ShipJumpState::Ready,
                  "ferry has not completed committed travel");
            bob(w,home,ferry,900,"arrived ferry must retain Bob until disembark commit");
            check(w.submit_host_passenger_action(command(104,w,ferry,destination,
                Action::HostDisembarkPassengers,true)),"destination arrival submission refused");
            const auto saved=w.world().canonical_state();
            refuses([&]{w.step(FailPoint::AfterSnapshotWrite);},
                    "failed disembark commit should roll back");
            check(w.world().canonical_state()==saved,"failed disembark teleported Bob");
        }
        {
            PersistentWorld w(path,Simulation(99));
            bob(w,home,ferry,900,"uncommitted disembark must leave Bob aboard ferry");
            check(w.world().voyage_status(900)==1,"failed arrival finalized voyage");
            applied(w,104);
            bob(w,home,destination,0,"committed disembark lost destination custody");
            check(w.world().voyage_status(900)==2,"completed voyage receipt missing");
        }
        {
            PersistentWorld w(path,Simulation(99));
            bob(w,home,destination,0,"destination custody did not survive restart");
        }
        PersistentWorld::recover_copy(path,recovered);
        {
            PersistentWorld w(recovered,Simulation(99));
            bob(w,home,destination,0,"genesis journal replay duplicated/teleported Bob");
            check(w.world().voyage_status(900)==2,"genesis recovery lost destination receipt");
        }
        fs::remove(recovered);fs::remove(path);
        std::cout<<"PASS: boarding, failed submissions, interrupted departure, mid-flight crashes, "
                    "uncommitted arrival, committed arrival, restart, independent replay: one Bob\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"DEMOG-05q6d1 failure: "<<e.what()<<'\n';return 1;}
}
