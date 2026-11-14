#include "galaxy/Stage07SocietyEconomy.hpp"
#include "galaxy/RegionalModeAuthority.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <cstdint>

using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(condition) do { if(!(condition)) throw std::runtime_error( \
    std::string("POP04 original society line ")+std::to_string(__LINE__)+": " #condition); } while(false)
template<class F> static void refuse(F&& f){
    bool denied=false;
    try{f();}catch(const std::exception&){denied=true;}
    CHECK(denied);
}
int main(){
    const auto root=(fs::temp_directory_path()/
      ("sg_pop04_society_"+std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count())));
    const auto path=root.string()+".sqlite",saved=path+".recovered";
    EntityId home=0;
    std::uint64_t original_generation=0,observed_named_npc=0;
    std::int64_t original_mass=0;
    try {
        {
            Simulation sim(8902);
            home=sim.add_colony_ship(7,{0,0},100,12);
            sim.provision_colony_genesis(
                home,100000,100000,100000,100000,100000);
            sim.enable_individual_workforce();
            sim.upgrade_assignment_snapshot_format();
            sim.enable_passenger_life_support();
            original_mass=sim.total_material_kg();
            PersistentWorld w(path,std::move(sim));
            w.establish_colony_conflict_agenda(
                8902,home,1,colony_conflict::Agenda::Cooperative);
            refuse([&]{(void)stage7::assess_original_society(w,8,home);});
            refuse([&]{(void)stage7::assess_original_society(w,7,99999);});
            refuse([&]{(void)stage7::assess_original_society(w,7,home,99999);});
            const auto initial=stage7::assess_original_society(w,7,home);
            CHECK(initial.population.named_living==12);
            CHECK(initial.population.registered_dependents==0);
            CHECK(initial.effective_governance.leader_id==1);
            CHECK(initial.effective_governance.agenda==
                  colony_conflict::Agenda::Cooperative);
            CHECK(initial.economy.ship_food_kg==100000);
            CHECK(!initial.economy.actual_workshop);
            refuse([&]{(void)stage7::observe_original_society(w,7,home,33);});
            refuse([&]{(void)stage7::observe_original_society(w,8,home,3);});
            const auto early=stage7::observe_original_society(w,7,home,3);
            CHECK(early.observed.status==
                  PersistentWorld::HostVoluntaryDepartureStatus::NotDue);
            w.steps(8);
            const auto pulse=stage7::observe_original_society(w,7,home,3);
            CHECK(pulse.before.tick==8 && pulse.after.tick==8);
            CHECK(pulse.observed.examined==3 && pulse.observed.observed==3);
            CHECK(pulse.observed.considering_departure==0);
            CHECK(!pulse.observed.admission_event_id && !pulse.observed.selected_npc);
            CHECK(w.world().npc_records().size()==12);
            CHECK(w.world().total_material_kg()==original_mass);
            CHECK(pulse.before.economy.ship_food_kg==
                  pulse.after.economy.ship_food_kg);
            CHECK(pulse.before.population.named_living==
                  pulse.after.population.named_living);
            const auto again=stage7::observe_original_society(w,7,home,3);
            CHECK(again.observed.examined==3 && again.observed.observed==0);
            // The original finite scheduler rotates a scan budget of 3
            // across 11 eligible adults. There is no contractual promise
            // that named NPC #2 is in this initial slice.
            for(const auto& [id,npc]:w.world().npc_records()){
                if(npc.home_ship==home && id!=1 &&
                   w.colony_personal_outlook(home,id)){
                    observed_named_npc=id;break;
                }
            }
            CHECK(observed_named_npc!=0);
            original_generation=w.generation();
        }
        {
            PersistentWorld reopened(path,Simulation(8903));
            CHECK(reopened.generation()==original_generation);
            CHECK(reopened.world().total_material_kg()==original_mass);
            CHECK(reopened.colony_personal_outlook(home,observed_named_npc).has_value());
            const auto facts=stage7::assess_original_society(reopened,7,home);
            CHECK(facts.population.named_living==12);
            CHECK(facts.effective_governance.leader_id==1);
        }
        PersistentWorld::recover_copy(path,saved);
        {
            PersistentWorld copy(saved,Simulation(8904));
            CHECK(copy.generation()==original_generation);
            CHECK(copy.world().total_material_kg()==original_mass);
            CHECK(copy.colony_personal_outlook(home,observed_named_npc).has_value());
            CHECK(stage7::assess_original_society(copy,7,home)
                  .population.named_living==12);
        }
        fs::remove(saved);fs::remove(path);
        std::cout<<"STAGE7 POP04 genuine autonomous social observation, finite economic "
                    "state, effective original governance, journal and restart passed\n";
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"STAGE7 POP04 native FAIL "<<e.what()<<"\n";
        return 1;
    }
}
