#include "galaxy/ShipHailJournal.hpp"
#include "galaxy/SecurityDeskHost.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace galaxy;
using namespace galaxy::ship_comms;
using namespace galaxy::colony_disposition;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(#x)+" at line "+std::to_string(__LINE__));}while(false)
int main(){
 const auto dir=fs::temp_directory_path()/
     ("sg_hail07e_"+std::to_string(
       std::chrono::steady_clock::now().time_since_epoch().count()));
 try{
    fs::create_directories(dir);
    const auto db=(dir/"world.sqlite").string();
    const auto copy=(dir/"recovered.sqlite").string();
    EntityId a=0,b=0,c=0;
    std::string saved;
    {
        Simulation origin(70707);
        a=origin.add_colony_ship(7,{0,0},100,6);
        b=origin.add_colony_ship(8,{1000,0},100,6);
        c=origin.add_colony_ship(9,{1500,0},100,6);
        for(const auto ship:{a,b,c})
            origin.provision_colony_genesis(
                ship,2000,2000,2000,2000,2000);
        origin.enable_individual_workforce();
        origin.upgrade_assignment_snapshot_format();
        origin.enable_passenger_life_support();
        PersistentWorld world(db,std::move(origin));
        TerritoryJournal land(db);
        Journal calls(db);
        CHECK(calls.history().empty());
        CHECK(calls.call(world,land,8,a,b,1000).status==Status::Denied);
        CHECK(calls.call(world,land,7,a,b,1001).status==Status::CallSent);
        CHECK(calls.history().size()==1);
        CHECK(calls.pending_for(world,8,b).size()==1);
        CHECK(calls.pending_for(world,7,a).empty());
        CHECK(calls.call(world,land,7,a,b,1002).status==Status::Suppressed);
        CHECK(calls.history().size()==1);
        CHECK(calls.reply(world,7,b,1001,1003,true).status==Status::Denied);
        CHECK(calls.reply(world,8,b,1001,1003,true).status==Status::Identified);
        CHECK(calls.pending_for(world,8,b).empty());
        CHECK(calls.history().size()==2);
        CHECK(calls.reply(world,8,b,1001,1004,true).status==Status::Denied);
        const auto first=calls.call(world,land,7,a,b,1005);
        CHECK(first.status==Status::Penalized);
        CHECK(first.penalty_points==1 &&
              first.cooldown_until==world.world().tick()+200 &&
              !first.law_review_eligible);
        CHECK(calls.history().size()==3);
        CHECK(calls.call(world,land,7,a,b,1006).status==Status::Suppressed);
        CHECK(calls.history().size()==3);
        CHECK(calls.call(world,land,7,a,c,1007).status==Status::Suppressed);
        CHECK(calls.pending_for(world,9,c).empty());
        // Continued harassment, even during a current account mute, is
        // recorded only once per committed tick; the call is NEVER sent.
        world.step();
        const auto second=calls.call(world,land,7,a,b,1008);
        CHECK(second.status==Status::Penalized &&
              second.penalty_points==2);
        world.step();
        const auto third=calls.call(world,land,7,a,b,1009);
        CHECK(third.status==Status::Penalized &&
              third.penalty_points==3 &&
              third.law_review_eligible);
        CHECK(calls.pending_for(world,8,b).empty());
        CHECK(calls.penalty_for(world,7,a).penalty_points==3);
        CHECK(calls.penalty_for(world,8,b).penalty_points==0);
        CHECK(calls.call(world,land,7,a,c,1010).status==Status::Suppressed);
        // Review eligibility is a recorded security complaint, not a
        // judgment or a license to arrest or fire.
        CHECK(world.world().npc_flight_orders().empty());
        CHECK(world.world().find(a)->hull>0);
        CHECK(world.world().find(b)->hull>0);
        // Refusal leaves recipient identity undisclosed and mutes new
        // unsolicited hails temporarily without inventing a crime score.
        CHECK(calls.call(world,land,9,c,b,1011).status==Status::CallSent);
        CHECK(calls.reply(world,8,b,1011,1012,false).status==Status::Declined);
        CHECK(calls.call(world,land,9,c,b,1013).status==Status::Suppressed);
        CHECK(calls.penalty_for(world,9,c).penalty_points==0);
        // Preexisting authenticated voluntary territorial identity counts
        // as identification to the physical claiming colony, even if a
        // direct ship-to-ship reply was never sent.
        CHECK(land.claim(world,7,a,2001,3000));
        std::uint64_t guard=0;
        for(const auto& [id,npc]:world.world().npc_records())
            if(npc.home_ship==a && npc.location==a &&
               world.world().npc_available_for_duty(id)){
                guard=id;break;
            }
        CHECK(guard);
        CHECK(land.voluntary_hail(world,9,a,c,guard,2002));
        const auto already_known=calls.call(world,land,7,a,c,2003);
        CHECK(already_known.status==Status::Penalized);
        CHECK(already_known.penalty_points==4);
        CHECK(calls.penalty_for(world,7,a).penalty_points==4);
        CHECK(calls.history().back().proof_kind==2);
        CHECK(calls.history().back().related==2002);
        CHECK(calls.pending_for(world,9,c).empty());
        land.verify();
        calls.verify();
        const auto old_count=calls.history().size();
        sqlite3* raw=nullptr;
        CHECK(sqlite3_open(db.c_str(),&raw)==SQLITE_OK);
        CHECK(sqlite3_exec(raw,
            "UPDATE sg_ship_hail07e SET penalty=0 WHERE event_id=1009",
            nullptr,nullptr,nullptr)!=SQLITE_OK);
        CHECK(sqlite3_exec(raw,
            "DELETE FROM sg_ship_hail07e WHERE event_id=1009",
            nullptr,nullptr,nullptr)!=SQLITE_OK);
        CHECK(sqlite3_close(raw)==SQLITE_OK);
        CHECK(calls.history().size()==old_count);
        saved=world.world().canonical_state();
        PersistentWorld::recover_copy(db,copy);
    }
    {
        PersistentWorld recovered(copy,Simulation(1));
        TerritoryJournal land(copy);
        Journal calls(copy);
        CHECK(recovered.world().canonical_state()==saved);
        calls.verify();
        CHECK(calls.penalty_for(recovered,7,a).penalty_points==4);
        CHECK(calls.penalty_for(recovered,7,a).law_review_eligible);
        CHECK(calls.pending_for(recovered,8,b).empty());
        CHECK(calls.call(recovered,land,7,a,c,1100).status==Status::Suppressed);
        // A malicious caller may keep trying after restart. Keep only ten
        // individually immutable abuse events per recipient, then continue
        // scoring through an atomic one-row compact total. Multiple target
        // scores sum into the same authenticated player's total.
        for(std::uint64_t i=0;i<7;++i){
            recovered.step();
            const auto strike=calls.call(
                recovered,land,7,a,b,1200+i);
            CHECK(strike.status==Status::Penalized);
            CHECK(strike.penalty_points==5+i);
        }
        const auto filled=calls.history().size();
        CHECK(calls.overflow_history().empty());
        recovered.step();
        const auto eleventh=calls.call(recovered,land,7,a,b,1300);
        CHECK(eleventh.status==Status::Penalized &&
              eleventh.penalty_points==12);
        CHECK(calls.history().size()==filled);
        CHECK(calls.overflow_history().size()==1);
        CHECK(calls.overflow_history().front().total_points==11);
        CHECK(calls.overflow_history().front().last_event==1300);
        CHECK(calls.penalty_for(recovered,7,a).penalty_points==12);
        CHECK(calls.call(recovered,land,7,a,b,1301).status==
              Status::Suppressed); // one score per target per game tick
        recovered.step();
        const auto twelfth=calls.call(recovered,land,7,a,b,1302);
        CHECK(twelfth.status==Status::Penalized &&
              twelfth.penalty_points==13);
        CHECK(calls.history().size()==filled);
        CHECK(calls.overflow_history().size()==1);
        CHECK(calls.overflow_history().front().total_points==12);
        CHECK(calls.penalty_for(recovered,7,a).penalty_points==13);
        CHECK(calls.history().size()==filled); // ten immutable details fixed
        {
            sqlite3* raw=nullptr;
            CHECK(sqlite3_open(copy.c_str(),&raw)==SQLITE_OK);
            CHECK(sqlite3_exec(raw,
                "UPDATE sg_ship_hail07e_totals SET points=2 "
                "WHERE recipient_ship>0",nullptr,nullptr,nullptr)!=SQLITE_OK);
            CHECK(sqlite3_exec(raw,
                "DELETE FROM sg_ship_hail07e_totals WHERE recipient_ship>0",
                nullptr,nullptr,nullptr)!=SQLITE_OK);
            CHECK(sqlite3_close(raw)==SQLITE_OK);
        }
        CHECK(calls.overflow_history().front().total_points==12);
        CHECK(recovered.world().npc_flight_orders().empty());
        calls.verify();
        // Actual compact penalty state, unlike the immutable first ten
        // detailed receipts, keeps accumulating after a native restart.
        const auto late=(dir/"late.sqlite").string();
        PersistentWorld::recover_copy(copy,late);
        {
            PersistentWorld late_world(late,Simulation(5));
            Journal late_calls(late);
            CHECK(late_calls.history().size()==filled);
            CHECK(late_calls.overflow_history().size()==1);
            CHECK(late_calls.penalty_for(late_world,7,a).penalty_points==13);
            late_calls.verify();
        }
    }
    fs::remove_all(dir);
    std::cout<<"PASS SHIP-STATUS-07E: physical authenticated direct hail, refusal and identification, post-identification hardcoded spam penalty, global mute, security review referral, immutable SQLite copy/restart and no invented police force\n";
    return 0;
 }catch(const std::exception& error){
    std::cerr<<"FAIL SHIP-STATUS-07E "<<error.what()<<'\n';
    std::error_code ignored;fs::remove_all(dir,ignored);
    return 1;
 }
}
