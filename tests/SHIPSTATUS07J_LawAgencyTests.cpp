#include "galaxy/ColonyLawAgency.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(#x)+" at "+std::to_string(__LINE__));}while(false)
static std::uint64_t duty(const Simulation& sim,EntityId ship){
    for(const auto& [id,npc]:sim.npc_records())
        if(npc.home_ship==ship && npc.location==ship &&
            npc.role==NpcRole::Ship && !npc.transport_id &&
            sim.npc_available_for_duty(id))return id;
    return 0;
}
int main(){
 const auto root=fs::temp_directory_path()/(
    "sg_status07j_"+std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count()));
 try{
    fs::create_directories(root);
    const auto db=(root/"world.sqlite").string();
    const auto backup=(root/"recover.sqlite").string();
    EntityId a=0,b=0,c=0;
    std::string canonical;
    {
        Simulation initial(707200);
        a=initial.add_colony_ship(7,{0,0},100,8);
        b=initial.add_colony_ship(8,{800,0},100,8);
        c=initial.add_colony_ship(9,{1600,0},100,8);
        for(const auto ship:{a,b,c})
            initial.provision_colony_genesis(
                ship,2000,2000,2000,2000,2000);
        initial.provision_starter_exploration_scanner(b);
        initial.enable_individual_workforce();
        initial.upgrade_assignment_snapshot_format();
        initial.enable_passenger_life_support();
        PersistentWorld world(db,std::move(initial));
        colony_disposition::TerritoryJournal territory(db);
        ship_comms::Journal calls(db);
        colony_law::Agency agency(db);
        ship_notifications::Outbox outbox(db);
        CHECK(!agency.active(world,8,b).event_id);
        CHECK(!agency.establish(world,territory,7,b,4000));
        CHECK(!agency.establish(world,territory,8,b,4001));
        const auto leader=duty(world.world(),b);
        CHECK(leader);
        CHECK(territory.claim(world,8,b,3001,3000));
        CHECK(!agency.establish(world,territory,8,b,4001));
        world.establish_colony_conflict_agenda(
            3050,b,leader,colony_conflict::Agenda::Cooperative);
        CHECK(world.colony_governance(b).leader_id==leader);
        CHECK(agency.establish(world,territory,8,b,4001));
        CHECK(!agency.establish(world,territory,8,b,4002));
        const auto appointment=agency.active(world,8,b);
        CHECK(appointment.owner==8&&appointment.colony==b);
        CHECK(appointment.government_event==
              world.colony_governance(b).event_id);
        CHECK(appointment.leader==leader && appointment.officer &&
              appointment.officer!=leader);
        CHECK(!agency.active(world,7,b).event_id);
        CHECK(!agency.refer(world,territory,calls,8,b,b,99999,7000));
        CHECK(!agency.refer(world,territory,calls,7,b,b,99999,7000));
        CHECK(calls.call(world,territory,7,a,b,5001).status==
            ship_comms::Status::CallSent);
        CHECK(calls.reply(world,8,b,5001,5002,true).status==
            ship_comms::Status::Identified);
        for(int i=0;i<3;++i){
            world.step();
            const auto strike=calls.call(world,territory,7,a,b,5003+i);
            CHECK(strike.status==ship_comms::Status::Penalized);
            CHECK(strike.penalty_points==std::uint64_t(i+1));
        }
        CHECK(!agency.refer(world,territory,calls,8,b,b,5003,7000));
        CHECK(!agency.refer(world,territory,calls,8,b,c,5005,7000));
        CHECK(!agency.refer(world,territory,calls,7,b,b,5005,7000));
        CHECK(agency.refer(world,territory,calls,8,b,b,5005,7000));
        CHECK(!agency.refer(world,territory,calls,8,b,b,5005,7001));
        const auto filings=agency.referrals(8,b);
        CHECK(filings.size()==1 && filings.front().complaint==5005);
        CHECK(filings.front().reported_caller==7 &&
              filings.front().points==3);
        CHECK(filings.front().reviewer && filings.front().officer &&
              filings.front().reviewer!=filings.front().officer &&
              filings.front().reviewer!=leader);
        CHECK(agency.referrals(7,b).empty());
        const auto recipient=outbox.pending(8);
        const auto filed=std::find_if(recipient.begin(),recipient.end(),
            [](const auto& n){return n.kind==7;});
        CHECK(filed!=recipient.end());
        CHECK(filed->source_event==7000 && filed->related==5005 &&
              filed->peer_owner==0);
        CHECK(!outbox.acknowledge(7,filed->sequence));
        // A verified nuisance complaint supplies no combat, police vessel,
        // arrest, seizure, damage, adjudication, or NPC conjuring authority.
        CHECK(world.world().npc_flight_orders().empty());
        CHECK(world.world().find(a)->hull>0);
        CHECK(world.world().find(b)->hull>0);
        const auto evidence=ship_comms::review::assess(
            world,territory,calls,8,b,b,5005,filings.front().reviewer);
        CHECK(evidence.status==ship_comms::review::Status::AwaitingLawAgency);
        CHECK(!evidence.criminal_guilt && !evidence.arrest_authorized &&
              !evidence.armed_intercept_authorized &&
              !evidence.property_seizure_authorized &&
              !evidence.police_dispatch_authorized);
        // Nonviolent investigation must use a real current physical contact.
        CHECK(!agency.investigate(world,territory,7,b,7000,a,8001));
        CHECK(!agency.investigate(world,territory,8,b,99999,a,8001));
        CHECK(!agency.investigate(world,territory,8,b,7000,c,8001));
        CHECK(world.world().npc_flight_orders().empty());
        CHECK(agency.investigate(world,territory,8,b,7000,a,8000));
        const auto& flights=world.world().npc_flight_orders();
        CHECK(flights.size()==1 && flights.count(8000)==1);
        const auto& flight=flights.at(8000);
        CHECK(flight.owner==8 && flight.ship==b &&
              flight.station==a && flight.pilot_npc==appointment.officer &&
              flight.purpose==NpcFlightPurpose::Patrol &&
              flight.mode==NpcFlightMode::LocalRendezvous &&
              flight.phase==NpcFlightPhase::Planned);
        const auto work=agency.investigations(world,8,b);
        CHECK(work.size()==1 && work.front().referral==7000 &&
              work.front().target_ship==a &&
              work.front().native_flight_present &&
              work.front().native_phase==NpcFlightPhase::Planned);
        CHECK(!agency.investigate(world,territory,8,b,7000,a,8002));
        CHECK(world.world().npc_flight_orders().size()==1);
        CHECK(world.world().find(a)->hull>0);
        CHECK(world.world().find(b)->hull>0);
        canonical=world.world().canonical_state();
        PersistentWorld::recover_copy(db,backup);
    }
    {
        PersistentWorld world(backup,Simulation(7));
        ship_comms::Journal calls(backup);
        colony_disposition::TerritoryJournal territory(backup);
        colony_law::Agency agency(backup);
        ship_notifications::Outbox outbox(backup);
        CHECK(world.world().canonical_state()==canonical);
        CHECK(agency.appointments().size()==1 &&
              agency.appointments().front().event_id==4001);
        CHECK(agency.referrals(8,b).size()==1);
        const auto recovered=agency.investigations(world,8,b);
        CHECK(recovered.size()==1 && recovered.front().native_flight_present &&
              recovered.front().order_id==8000 &&
              recovered.front().officer==agency.appointments().front().officer);
        CHECK(agency.max_event()>=8000);
        CHECK(world.world().npc_flight_orders().count(8000)==1);
        CHECK(agency.referrals(7,b).empty());
        const auto saved=outbox.pending(8);
        CHECK(std::any_of(saved.begin(),saved.end(),
            [](const auto& n){return n.kind==7 && n.source_event==7000;}));
        CHECK(!agency.refer(world,territory,calls,8,b,b,5005,7002));
        sqlite3* raw=nullptr;
        CHECK(sqlite3_open(backup.c_str(),&raw)==SQLITE_OK);
        CHECK(sqlite3_exec(raw,
            "DELETE FROM sg_law_referrals07j",nullptr,nullptr,nullptr)
            !=SQLITE_OK);
        CHECK(sqlite3_exec(raw,
            "UPDATE sg_colony_law07j SET owner=7",nullptr,nullptr,nullptr)
            !=SQLITE_OK);
        sqlite3_close(raw);
    }
    fs::remove_all(root);
    std::cout<<"PASS STATUS-07J: independently appointed named agency, "
        "real jurisdiction and third-party complaint review, owner denial, "
        "no force, SQLite recovery and immutable referral\n";
    return 0;
 }catch(const std::exception& e){
    std::cerr<<"FAIL STATUS-07J "<<e.what()<<'\n';
    std::error_code ignored;fs::remove_all(root,ignored);return 1;
 }
}
