#include "galaxy/ColonyDispositionCoordinator.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace galaxy;
using namespace galaxy::colony_disposition;
#define CHECK(v) do {if(!(v))throw std::runtime_error(std::string(#v)+" line "+std::to_string(__LINE__));}while(false)
int main(){
    namespace fs=std::filesystem;
    const auto path=(fs::temp_directory_path()/("sg_disposition01_"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".sqlite")).string();
    auto clean=[&]{for(const char* s:{"","-wal","-shm"})fs::remove(path+s);};
    try {
        Simulation seed(110621);
        const auto isolation=seed.add_colony_ship(7,{0,0},100,6);
        const auto cooperative=seed.add_colony_ship(9,{0,2000},100,3);
        const auto predatory=seed.add_colony_ship(10,{0,-2000},100,5);
        const auto visitor=seed.add_ship(8,{1000,0},100);
        for(const auto ship:{isolation,cooperative,predatory})
            seed.provision_colony_genesis(ship,1500,1500,1500,1500,1500);
        seed.enable_individual_workforce();
        seed.upgrade_assignment_snapshot_format();
        seed.enable_passenger_life_support();
        const auto matter=seed.total_material_kg();
        std::uint64_t iso_leader=0,iso_watch=0,coop_leader=0,
                      coop_watch=0,pred_leader=0,pred_watch=0;
        std::string final_state;
        {
            PersistentWorld world(path,std::move(seed));
            auto crew=[&](EntityId ship){
                std::vector<std::uint64_t> ids;
                for(const auto& [id,n]:world.world().npc_records())
                    if(n.location==ship && n.home_ship==ship &&
                       world.world().npc_available_for_duty(id))
                        ids.push_back(id);
                return ids;
            };
            auto i=crew(isolation),c=crew(cooperative),p=crew(predatory);
            CHECK(i.size()==6 && c.size()==3 && p.size()==5);
            iso_leader=i[0];iso_watch=i[1];
            coop_leader=c[0];coop_watch=c[1];
            pred_leader=p[0];pred_watch=p[1];
            world.establish_colony_conflict_agenda(81001,isolation,iso_leader,
                colony_conflict::Agenda::Isolationist);
            world.establish_colony_conflict_agenda(81002,cooperative,coop_leader,
                colony_conflict::Agenda::Cooperative);
            world.establish_colony_conflict_agenda(81003,predatory,pred_leader,
                colony_conflict::Agenda::Predatory);
            world.record_colony_watch_contact(82001,isolation,iso_watch,visitor);
            world.record_colony_watch_contact(82002,cooperative,coop_watch,visitor);
            world.record_colony_watch_contact(82003,predatory,pred_watch,visitor);
            const auto wary=assess(world,isolation,82001);
            const auto peaceful=assess(world,cooperative,82002);
            const auto outlaw=assess(world,predatory,82003);
            CHECK(wary.decision.agenda==colony_conflict::Agenda::Isolationist);
            CHECK(wary.decision.posture==colony_conflict::Posture::Warn);
            CHECK(wary.threat.intent==npc_response::ThreatIntent::WarnLeave);
            CHECK(wary.movement_warranted);
            CHECK(peaceful.decision.posture==colony_conflict::Posture::Observe);
            CHECK(peaceful.threat.intent==npc_response::ThreatIntent::Ignore);
            CHECK(!peaceful.movement_warranted);
            CHECK(outlaw.decision.agenda==colony_conflict::Agenda::Predatory);
            CHECK(outlaw.decision.motive==colony_conflict::Motive::Predation);
            // Lacking manufactured modules, this actual criminal colony
            // can only WARN; the coordinator cannot invent a raid fleet.
            CHECK(outlaw.decision.posture==colony_conflict::Posture::Warn);
            const auto no_force=[&](const Evaluation& e){
                return !e.threat.can_attack && !e.threat.can_arrest &&
                       !e.threat.can_seize_goods &&
                       !e.threat.legal_crime_finding;
            };
            CHECK(no_force(wary) && no_force(peaceful) && no_force(outlaw));
            const auto skipped=dispatch_witnessed_response(
                world,cooperative,82002,83002,84002,coop_watch);
            CHECK(skipped.status==npc_response::Status::Refused);
            CHECK(!skipped.intent_recorded);
            CHECK(!world.world().npc_flight_orders().count(84002));
            const auto response=dispatch_witnessed_response(
                world,isolation,82001,83001,84001,iso_watch);
            CHECK(response.intent_recorded);
            CHECK(response.status==npc_response::Status::Accepted);
            CHECK(world.world().npc_flight_orders().at(84001).purpose==
                  NpcFlightPurpose::Patrol);
            CHECK(world.world().npc_flight_orders().at(84001).ship==isolation);
            CHECK(world.world().npc_flight_orders().at(84001).station==visitor);
            CHECK(world.world().npc_records().size()==14);
            CHECK(world.world().total_material_kg()==matter);
            CHECK(world.world().find(visitor)->owner_id==8);
            bool arrived=false;
            for(int tick=0;tick<400;++tick){
                world.step();
                if(world.world().npc_flight_orders().at(84001).phase==
                        NpcFlightPhase::Arrived){arrived=true;break;}
            }
            CHECK(arrived);
            CHECK(world.world().npc_flight_orders().at(84001).last_local_command!=0);
            CHECK(world.world().find(visitor)->hull==100);
            CHECK(world.world().total_material_kg()==matter);
            final_state=world.world().canonical_state();
            CHECK(Simulation::from_snapshot(final_state).canonical_state()==final_state);
        }
        {
            PersistentWorld restarted(path,Simulation(1));
            CHECK(restarted.world().canonical_state()==final_state);
            CHECK(restarted.world().npc_flight_orders().at(84001).phase==
                  NpcFlightPhase::Arrived);
            CHECK(restarted.world().find(visitor)->hull==100);
            CHECK(restarted.world().total_material_kg()==matter);
            CHECK(restarted.world().npc_records().size()==14);
        }
        clean();
        std::cout<<"PASS DISPOSITION-01: durable original diplomacy, witnessed physical patrol, no invented police/raiding power and restart\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL DISPOSITION-01: "<<e.what()<<'\n';
        clean();return 1;
    }
}
