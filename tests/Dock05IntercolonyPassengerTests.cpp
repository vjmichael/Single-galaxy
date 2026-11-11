// DOCK-05: source-native real passenger ferry and visited station, staged
// for one later consolidated Windows/Linux matrix. Not executed on creation.
#include "galaxy/Dock05Voyage.hpp"
#include "galaxy/Dock05RegionalDemand.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sqlite3.h>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("DOCK05 at line ")+std::to_string(__LINE__)+": " #x);}while(0)
static void clean(const std::string& path){
    for(const auto* extension:{"","-wal","-shm"}){
        std::error_code ec;fs::remove(path+extension,ec);
    }
}
static void dock(PersistentWorld& world,EntityId ship,
                 EntityId station,std::uint64_t cmd){
    Command c{};
    c.id=cmd;c.sequence=cmd;c.issuer=7;
    c.actor=ship;c.target=station;
    c.action=Action::RequestDock;
    c.execute_tick=world.world().tick()+1;
    CHECK(world.submit(c));
    world.step();
    CHECK(world.world().events().back().command_id==cmd);
    CHECK(world.world().events().back().result==Outcome::Applied);
    world.steps(Simulation::kDockTransitionTicks);
    CHECK(world.world().find(ship)->dock_state==DockState::Docked);
    CHECK(world.world().find(ship)->dock_station==station);
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    const std::string db=argv[1];
    clean(db);
    try{
        EntityId from=0,to=0,home=0,ferry=0,receiving=0;
        std::uint64_t employee=0,guest=0,manifest=0,route=0;
        std::size_t original_count=0;
        {
            Simulation initial(0xD055);
            from=initial.add_station({0,0});
            to=initial.add_station({6000,0});
            home=initial.add_colony_ship(7,{0,0},100,8);
            ferry=initial.add_colony_ship(7,{0,0},100,5);
            receiving=initial.add_colony_ship(7,{6000,0},100,5);
            initial.provision_colony_genesis(home,1000,3000,1000,1000,1000);
            initial.provision_colony_genesis(ferry,1000,3000,1000,1000,1000);
            initial.provision_colony_genesis(receiving,1000,3000,1000,1000,1000);
            initial.enable_individual_workforce();
            initial.upgrade_assignment_snapshot_format();
            initial.enable_passenger_life_support();
            initial.enable_native_galactic_physics(
                {{1,galactic::Point{{0,100000},{0,100000},{0,100000}}}});
            for(const auto& [id,n]:initial.npc_records())
                if(n.home_ship==home){
                    if(!employee)employee=id;
                    else if(!guest){guest=id;break;}
                }
            CHECK(employee && guest && employee!=guest);
            original_count=initial.npc_records().size();
            PersistentWorld world(db,std::move(initial));
            dock(world,home,from,31501);
            dock(world,ferry,from,31502);
            dock(world,receiving,to,31503);
            dock::Ledger staff(db);
            dock04::Traffic boarding(db);
            dock05::Routes journeys(db);
            dock05::RegionalPopulation cold(db);
            CHECK(!cold.projection(world.world(),from,7,32).source_bound);
            CHECK(journeys.destinations(world.world(),from,7).size()==1);
            CHECK(journeys.destinations(world.world(),from,7).front().station==to);
            CHECK(world.station_dock_worker_transfer(
                7,home,from,employee,true,31504));
            CHECK(staff.assign(world.world(),from,7,employee));
            CHECK(boarding.enqueue(world.world(),from,7,guest,ferry));
            CHECK(boarding.serve_one(world,from,7,employee));
            manifest=world.world().npc_records().at(guest).transport_id;
            CHECK(manifest>0);
            CHECK(world.world().voyage_status(manifest)==1);
            CHECK(!journeys.book(world,from,7,ferry,from,receiving));
            CHECK(!journeys.book(world,from,8,ferry,to,receiving));
            CHECK(!journeys.book(world,from,7,ferry,to,home));
            route=journeys.book(world,from,7,ferry,to,receiving);
            CHECK(route>0);
            CHECK(!journeys.book(world,from,7,ferry,to,receiving));
            CHECK(journeys.status(from,7).active==1);
            CHECK(journeys.status(from,7).passengers_travelling==1);
            CHECK(world.world().npc_records().size()==original_count);
            CHECK(!world.world().has_original_station_guest_ashore(receiving));
        }
        // Genuine restart BEFORE flight begins: exact route/manifest survives
        // without automatically departing or minting a second passenger.
        {
            PersistentWorld world(db,Simulation(0xD056));
            dock05::Routes journeys(db);
            CHECK(world.world().npc_records().at(guest).transport_id==manifest);
            CHECK(journeys.status(from,7).active==1);
            CHECK(world.world().passengers_aboard(ferry)==1);
            bool delivered=false;
            for(int step=0;step<1600;++step){
                if(journeys.status(from,7).delivered>0){
                    delivered=true;break;
                }
                if(!journeys.advance(world,from,7))break;
            }
            CHECK(delivered || journeys.status(from,7).delivered==1);
            CHECK(world.world().voyage_status(manifest)==2);
            CHECK(world.world().npc_records().at(guest).location==to);
            CHECK(world.world().npc_records().at(guest).transport_id==0);
            CHECK(world.world().original_completed_ferry_guest(
                guest,receiving,manifest));
            CHECK(world.world().has_original_station_guest_ashore(receiving));
            CHECK(world.world().npc_records().size()==original_count);
            CHECK(world.world().assess_npc_residence().reconciled);
            CHECK(journeys.status(from,7).active==0);
            CHECK(journeys.status(from,7).delivered==1);
            CHECK(journeys.status(from,7).visitors_arrived==1);
            const auto before=world.world().canonical_state();
            CHECK(!journeys.advance(world,from,7));
            CHECK(world.world().canonical_state()==before);
        }
        // Actual original SQLite recovery must independently validate the
        // original completed ferry receipt plus the latest station visit.
        {
            PersistentWorld world(db,Simulation(0xD057));
            dock05::Routes journeys(db);
            CHECK(world.world().npc_records().at(guest).location==to);
            CHECK(world.world().npc_records().at(guest).home_ship==home);
            CHECK(world.world().original_completed_ferry_guest(
                guest,receiving,manifest));
            CHECK(journeys.status(from,7).delivered==1);
            CHECK(world.world().npc_records().size()==original_count);
            CHECK(!journeys.cancel_unstarted(world,from,7,route));
            const auto exact=world.world().canonical_state();
            bool injected=false;
            try{
                (void)world.station_visitor_transfer(7,receiving,to,guest,
                    manifest,false,5900001,FailPoint::AfterReceiptWrite);
            }catch(const std::exception&){injected=true;}
            CHECK(injected);
            CHECK(world.world().canonical_state()==exact);
            CHECK(world.world().npc_records().at(guest).location==to);
            // No destination visit is completed by cohort projection,
            // an arbitrary booking receipt or visual NPC position.
            dock05::RegionalPopulation cold(db);
            const auto absent=cold.projection(world.world(),from,7,32);
            CHECK(absent.population==0 && absent.demand==0 &&
                  !absent.source_bound);
        }
        // Independently falsify the source visitor receipt in a COPY.
        // Original world survives intact. A forged station resident must
        // never be accepted solely because their coordinates look plausible.
        const std::string forged=db+".forged";
        {
            sqlite3* from_db=nullptr,*to_db=nullptr;
            CHECK(sqlite3_open_v2(db.c_str(),&from_db,SQLITE_OPEN_READONLY,nullptr)==SQLITE_OK);
            CHECK(sqlite3_open_v2(forged.c_str(),&to_db,
                SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE,nullptr)==SQLITE_OK);
            sqlite3_backup* backup=sqlite3_backup_init(to_db,"main",from_db,"main");
            CHECK(backup!=nullptr);
            CHECK(sqlite3_backup_step(backup,-1)==SQLITE_DONE);
            CHECK(sqlite3_backup_finish(backup)==SQLITE_OK);
            sqlite3_close(to_db);sqlite3_close(from_db);
        }
        {
            sqlite3* attack=nullptr;
            CHECK(sqlite3_open(forged.c_str(),&attack)==SQLITE_OK);
            sqlite3_stmt* erase=nullptr;
            CHECK(sqlite3_prepare_v2(attack,
                "DELETE FROM sg_dock05_visitor_moves WHERE npc_id=?",
                -1,&erase,nullptr)==SQLITE_OK);
            CHECK(sqlite3_bind_int64(erase,1,
                static_cast<sqlite3_int64>(guest))==SQLITE_OK);
            CHECK(sqlite3_step(erase)==SQLITE_DONE);
            sqlite3_finalize(erase);sqlite3_close(attack);
            bool rejected=false;
            try{PersistentWorld invalid(forged,Simulation(0xD058));}
            catch(const std::exception&){rejected=true;}
            CHECK(rejected);
        }
        clean(forged);
        clean(db);
        std::cout<<"DOCK05 staged native route/arrival/restart matrix PASS\n";
        return 0;
    }catch(const std::exception& error){
        std::cerr<<"DOCK05 staged native route/arrival matrix FAILED: "
                 <<error.what()<<'\n';
        clean(db);
        return 1;
    }
}
