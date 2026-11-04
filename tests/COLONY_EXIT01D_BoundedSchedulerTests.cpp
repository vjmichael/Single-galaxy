#include "galaxy/Persistence.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
using namespace galaxy;
#define CHECK(v) do{if(!(v))throw std::runtime_error(std::string("EXIT01D BOUNDED line ")+std::to_string(__LINE__)+": " #v);}while(false)
template<class Fn>void refuses(Fn&& f){bool refused=false;try{f();}catch(const std::exception&){refused=true;}CHECK(refused);}
static void clean(const std::string& p){for(auto* suffix:{"","-wal","-shm"})std::filesystem::remove(p+suffix);}
int main(){
try{
    const auto path=(std::filesystem::temp_directory_path()/
        ("exit_auto_scan_v89_"+std::to_string(std::chrono::steady_clock::now()
        .time_since_epoch().count())+".sqlite")).string();
    const auto recovered=path+".recovered",unfounded=path+".unfounded";
    EntityId source=0;
    {
        Simulation sim(8902);
        source=sim.add_colony_ship(7,{0,0},100,12);
        sim.provision_colony_genesis(
            source,100000,100000,100000,100000,100000);
        sim.enable_individual_workforce();
        sim.upgrade_assignment_snapshot_format();
        sim.enable_passenger_life_support();
        PersistentWorld w(path,std::move(sim));
        w.establish_colony_conflict_agenda(
            8902,source,1,colony_conflict::Agenda::Cooperative);
        PersistentWorld::HostVoluntaryDepartureSchedule request{};
        request.origin=source;request.scan_budget=3;
        CHECK(w.supervise_voluntary_migration(request).status==
              PersistentWorld::HostVoluntaryDepartureStatus::NotDue);
        refuses([&]{
            auto bad=request;bad.scan_budget=33;
            (void)w.supervise_voluntary_migration(bad);
        });
        std::set<std::uint64_t> observed;
        for(std::uint64_t due=8;due<=40;due+=8){
            w.steps(due-w.world().tick());
            const auto result=w.supervise_voluntary_migration(request);
            CHECK(result.tick==due);
            CHECK(result.examined==3);
            CHECK(result.observed==(due==40?3:due==32?2:3));
            CHECK(result.considering_departure==0);
            CHECK(w.world().colony_ship(source)->crew==12);
            const auto retry=w.supervise_voluntary_migration(request);
            CHECK(retry.observed==0 && retry.examined==3);
            CHECK(w.world().npc_casualties().empty());
            for(const auto& [id,npc]:w.world().npc_records())
                if(npc.home_ship==source &&
                   w.colony_personal_outlook(source,id))
                    observed.insert(id);
            // The host should not make any fake immigration/admission when
            // no foreign colony has agreed to receive these NPCs.
            CHECK(!result.admission_event_id);
            CHECK(!result.selected_npc);
            if(due<40){
                w.step();
                CHECK(w.supervise_voluntary_migration(request).status==
                      PersistentWorld::HostVoluntaryDepartureStatus::NotDue);
            }
        }
        CHECK(observed.size()==11); // only incumbent #1 is excluded
        CHECK(!w.colony_personal_outlook(source,1));
        CHECK(w.world().npc_records().size()==12);
    }
    {
        PersistentWorld replay(path,Simulation(8903));
        CHECK(replay.world().colony_ship(source)->crew==12);
        CHECK(replay.colony_personal_outlook(source,2).has_value());
        CHECK(!replay.colony_personal_outlook(source,1));
    }
    PersistentWorld::recover_copy(path,recovered);
    {
        PersistentWorld copy(recovered,Simulation(8904));
        CHECK(copy.world().colony_ship(source)->crew==12);
        CHECK(copy.colony_personal_outlook(source,2).has_value());
    }
    {
        Simulation sim(8905);
        const auto fresh=sim.add_colony_ship(7,{0,0},100,4);
        sim.provision_colony_genesis(
            fresh,1000,1000,1000,1000,1000);
        sim.enable_individual_workforce();
        PersistentWorld unchartered(unfounded,std::move(sim));
        unchartered.steps(8);
        PersistentWorld::HostVoluntaryDepartureSchedule missing{};
        missing.origin=fresh;
        const auto out=unchartered.supervise_voluntary_migration(missing);
        CHECK(out.status==
            PersistentWorld::HostVoluntaryDepartureStatus::NoResidents);
        CHECK(!unchartered.colony_personal_outlook(fresh,2));
    }
    clean(path);clean(recovered);clean(unfounded);
    std::cout<<"PASS EXIT01D BOUNDED: deterministic 8-tick rotation, limit 3 "
        "observations/tick, no duplicate diaries, no unauthenticated migration, "
        "unfounded governor refusal, no synthetic NPCs, restart/recovered copy\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
