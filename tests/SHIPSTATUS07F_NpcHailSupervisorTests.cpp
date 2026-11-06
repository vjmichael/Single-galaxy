#include "galaxy/NpcHailSupervisor.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(#x)+" at line "+std::to_string(__LINE__));}while(false)

static std::uint64_t actual_ship_operator(
    const Simulation& sim,EntityId home){
    for(const auto& [id,n]:sim.npc_records())
        if(n.role==NpcRole::Ship && n.home_ship==home &&
           n.location==home && !n.transport_id &&
           sim.npc_available_for_duty(id))return id;
    return 0;
}
int main(){
 const auto dir=fs::temp_directory_path()/
    ("sg_hail07f_"+std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
 try{
    fs::create_directories(dir);
    const auto db=(dir/"world.sqlite").string();
    const auto backup=(dir/"restarted.sqlite").string();
    EntityId player=0,cooperative=0,isolationist=0,predatory=0;
    std::uint64_t nonce=900000000010ULL;
    std::string canonical;
    std::size_t count=0;
    {
        Simulation origin(90707);
        player=origin.add_colony_ship(7,{0,0},100,6);
        cooperative=origin.add_colony_ship(8,{800,0},100,6);
        isolationist=origin.add_colony_ship(9,{900,0},100,6);
        predatory=origin.add_colony_ship(10,{1100,0},100,6);
        for(auto ship:{player,cooperative,isolationist,predatory})
            origin.provision_colony_genesis(
                ship,2000,2000,2000,2000,2000);
        origin.enable_individual_workforce();
        origin.upgrade_assignment_snapshot_format();
        origin.enable_passenger_life_support();
        PersistentWorld world(db,std::move(origin));
        colony_disposition::TerritoryJournal territory(db);
        ship_comms::Journal calls(db);
        const auto leader_coop=actual_ship_operator(
            world.world(),cooperative);
        const auto leader_iso=actual_ship_operator(
            world.world(),isolationist);
        const auto leader_pred=actual_ship_operator(
            world.world(),predatory);
        CHECK(leader_coop && leader_iso && leader_pred);
        world.establish_colony_conflict_agenda(910001,cooperative,
            leader_coop,colony_conflict::Agenda::Cooperative);
        world.establish_colony_conflict_agenda(910002,isolationist,
            leader_iso,colony_conflict::Agenda::Isolationist);
        world.establish_colony_conflict_agenda(910003,predatory,
            leader_pred,colony_conflict::Agenda::Predatory);
        CHECK(calls.call(world,territory,7,player,cooperative,920001).status==
              ship_comms::Status::CallSent);
        CHECK(calls.call(world,territory,7,player,isolationist,920002).status==
              ship_comms::Status::CallSent);
        CHECK(calls.call(world,territory,7,player,predatory,920003).status==
              ship_comms::Status::CallSent);
        const auto before=world.world().canonical_state();
        // A ship with a real authenticated human account is NEVER
        // treated as an NPC that can identify or decline on its behalf,
        // even when it also has named NPC crew and a governance charter.
        const auto protected_humans=npc_hail::supervise(
            world,calls,7,nonce,std::set<PlayerId>{7,8,9,10});
        CHECK(protected_humans.answered==0);
        CHECK(calls.history().size()==3);
        CHECK(calls.pending_for(world,8,cooperative).size()==1);
        CHECK(calls.pending_for(world,9,isolationist).size()==1);
        CHECK(calls.pending_for(world,10,predatory).size()==1);
        const auto pulse=npc_hail::supervise(world,calls,7,nonce);
        CHECK(pulse.answered==2 && pulse.identified==1 &&
              pulse.declined==1);
        CHECK(calls.pending_for(world,8,cooperative).empty());
        CHECK(calls.pending_for(world,9,isolationist).empty());
        CHECK(calls.pending_for(world,10,predatory).size()==1);
        CHECK(calls.history().size()==5); // three genuine calls + two replies
        CHECK(calls.history()[3].kind==ship_comms::Kind::Identify);
        CHECK(calls.history()[3].recipient_controller==8);
        CHECK(calls.history()[4].kind==ship_comms::Kind::Decline);
        CHECK(calls.history()[4].recipient_controller==0);
        CHECK(!calls.penalty_for(world,7,player).penalty_points);
        CHECK(world.world().canonical_state()==before); // no magic crew/world
        const auto again=npc_hail::supervise(world,calls,7,nonce);
        CHECK(again.answered==0);
        CHECK(calls.history().size()==5);
        const auto abused=calls.call(world,territory,
            7,player,cooperative,920004);
        CHECK(abused.status==ship_comms::Status::Penalized);
        CHECK(abused.penalty_points==1);
        CHECK(calls.call(world,territory,7,player,isolationist,920005).status==
              ship_comms::Status::Suppressed);
        CHECK(calls.pending_for(world,9,isolationist).empty());
        CHECK(calls.pending_for(world,10,predatory).size()==1);
        CHECK(world.world().npc_flight_orders().empty());
        CHECK(world.world().canonical_state()==before);
        calls.verify();
        canonical=world.world().canonical_state();
        count=calls.history().size();
        PersistentWorld::recover_copy(db,backup);
    }
    {
        PersistentWorld world(backup,Simulation(1));
        ship_comms::Journal calls(backup);
        CHECK(world.world().canonical_state()==canonical);
        CHECK(calls.history().size()==count);
        CHECK(calls.pending_for(world,10,predatory).size()==1);
        CHECK(calls.penalty_for(world,7,player).penalty_points==1);
        CHECK(calls.history().back().kind==ship_comms::Kind::Abuse);
        calls.verify();
    }
    fs::remove_all(dir);
    std::cout<<"PASS SHIP-STATUS-07F: genuine named-NPC governed identify/decline/silence response to two-sided original physical hails, no synthetic pilot/force, and SQLite recovery\n";
    return 0;
 }catch(const std::exception& e){
    std::cerr<<"FAIL SHIP-STATUS-07F "<<e.what()<<'\n';
    std::error_code ignored;fs::remove_all(dir,ignored);
    return 1;
 }
}
