#include "galaxy/Persistence.hpp"
#include "galaxy/ShipStatusFitting.hpp"
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do {if(!(x))throw std::runtime_error(std::string(#x)+" line "+std::to_string(__LINE__));}while(false)
static Command cmd(const PersistentWorld& w,std::uint64_t id,
                   EntityId ship,Action action,std::int64_t amount=0,
                   PlayerId issuer=7) {
    Command c{};
    c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
    c.issuer=issuer;c.actor=ship;c.action=action;c.amount=amount;
    return c;
}
static Outcome run(PersistentWorld& w,const Command& c) {
    CHECK(w.submit(c));w.step();
    CHECK(w.world().events().back().command_id==c.id);
    return w.world().events().back().result;
}
int main(){
    const auto dir=fs::temp_directory_path()/
       ("sg_shipstatus02_"+std::to_string(
         std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto db=(dir/"world.sqlite").string();
    const auto copy=(dir/"recovered.sqlite").string();
    try{
        fs::create_directories(dir);
        EntityId ship=0,attacker=0;
        std::uint64_t seed_for_attack=0,expected_mass=0;
        std::string checkpoint;
        {
            Simulation genesis(12002);
            ship=genesis.add_colony_ship(7,{0,0},100,6);
            attacker=genesis.add_ship(8,{1000,0},100);
            genesis.provision_starter_exploration_scanner(ship);
            genesis.provision_starter_probe_kit(ship);
            genesis.enable_individual_workforce();
            expected_mass=static_cast<std::uint64_t>(genesis.total_material_kg());
            const auto* fitted=genesis.find(ship);
            CHECK(fitted && fitted->fitting.hardpoints[0]==1 &&
                  fitted->fitting.hardpoints[1]==2);
            // Find an authentic deterministic seed which shuts off the scanner
            // for the first four ticks, then use that exact seed in the host.
            const auto controls=ship_status::fitted_controls(*fitted,
                {{true,true}},{{true,true}},{{false,false}});
            for(std::uint64_t seed=1;seed<1000 && !seed_for_attack;++seed) {
                ship_status::Engine temporary;
                ship_status::Effect e{};
                e.id=7001;e.source_ship=attacker;e.target_ship=ship;
                e.started_tick=0;e.expires_tick=8;e.pulse_ticks=4;
                e.seed=seed;e.intensity=100;
                CHECK(temporary.inflict_confusion(e,controls,0));
                if(!temporary.project(ship,0,controls)[0].effective_on)
                    seed_for_attack=seed;
            }
            CHECK(seed_for_attack);
            PersistentWorld w(db,std::move(genesis));
            CHECK(w.world().hardpoint_effective_on(ship,0));
            CHECK(w.world().hardpoint_effective_on(ship,1));
            CHECK(!w.submit_testing_ship_confusion(ship,ship,7000,1,8,4,100,0,3));
            CHECK(!w.submit_testing_ship_confusion(attacker,ship,7000,1,8,4,100,0,0));
            const auto pristine=w.world().canonical_state();
            const auto generation=w.generation();
            bool failed=false;
            try{
                (void)w.submit_testing_ship_confusion(
                    attacker,ship,7001,seed_for_attack,8,4,100,0,3,
                    FailPoint::AfterSnapshotWrite);
            }catch(const std::runtime_error&){failed=true;}
            CHECK(failed);
            CHECK(w.generation()==generation);
            CHECK(w.world().canonical_state()==pristine);
            {PersistentWorld independent(db,Simulation(55));
                CHECK(independent.world().canonical_state()==pristine);}
            CHECK(w.submit_testing_ship_confusion(
                attacker,ship,7001,seed_for_attack,8,4,100,0,3));
            CHECK(!w.submit_testing_ship_confusion(
                attacker,ship,7001,seed_for_attack,8,4,100,0,3));
            CHECK(w.world().status_effects().records().size()==1);
            CHECK(w.world().canonical_state().find("galaxy-kernel-v100|")==0);
            CHECK(!w.world().hardpoint_effective_on(ship,0));
            // Actual native SurveyLocal must refuse the disabled scanner.
            CHECK(run(w,cmd(w,1001,ship,Action::SurveyLocal))==Outcome::ModuleOffline);
            CHECK(w.world().discoveries().empty());
            CHECK(!w.submit(cmd(w,1099,ship,Action::SetHardpointPower,4)));
            CHECK(run(w,cmd(w,1002,ship,Action::SetHardpointPower,0))==Outcome::Applied);
            CHECK(!w.world().find(ship)->hardpoint_requested_on[0]);
            // During the same confusion pulse, a hacked module may be ON
            // even though the pilot just requested OFF.
            CHECK(w.world().hardpoint_effective_on(ship,0));
            CHECK(run(w,cmd(w,1003,ship,Action::SurveyLocal))==Outcome::Applied);
            CHECK(w.world().find(ship)->weapon_charges==12);
            checkpoint=w.world().canonical_state();
            CHECK(w.world().total_material_kg()==static_cast<std::int64_t>(expected_mass));
        }
        {
            PersistentWorld recovered(db,Simulation(400));
            CHECK(recovered.world().canonical_state()==checkpoint);
            CHECK(recovered.world().status_effects().records().size()==1);
            CHECK(recovered.world().find(ship)->hardpoint_requested_on[0]==false);
            CHECK(recovered.world().hardpoint_effective_on(ship,0));
            PersistentWorld::recover_copy(db,copy);
            recovered.steps(6); // tick3 -> tick9, effect ends at tick8
            CHECK(recovered.world().status_effects().records().size()==1);
            CHECK(!recovered.world().hardpoint_effective_on(ship,0));
            CHECK(run(recovered,cmd(recovered,1004,ship,Action::SurveyLocal))==
                  Outcome::ModuleOffline);
            CHECK(run(recovered,cmd(recovered,1005,ship,Action::SetHardpointPower,1))==
                  Outcome::Applied);
            CHECK(recovered.world().hardpoint_effective_on(ship,0));
            CHECK(run(recovered,cmd(recovered,1006,ship,Action::SurveyLocal))==
                  Outcome::Applied);
            // The probe launcher is a separate fitted hardpoint: OFF means
            // actual native launch is denied without consuming its real probe.
            CHECK(run(recovered,cmd(recovered,1007,ship,Action::SetHardpointPower,2))==
                  Outcome::Applied);
            CHECK(!recovered.world().hardpoint_effective_on(ship,1));
            CHECK(run(recovered,cmd(recovered,1008,ship,Action::DeploySurveyProbe))==
                  Outcome::ModuleOffline);
            CHECK(recovered.world().find(ship)->survey_probes_carried==1);
            CHECK(run(recovered,cmd(recovered,1009,ship,Action::SetHardpointPower,3))==
                  Outcome::Applied);
            CHECK(recovered.world().hardpoint_effective_on(ship,1));
            CHECK(run(recovered,cmd(recovered,1010,ship,Action::DeploySurveyProbe))==
                  Outcome::Applied);
            // Wrong-owner power controls must never mutate another ship.
            auto wrong=cmd(recovered,1011,ship,Action::SetHardpointPower,0,8);
            const auto original_intent=
                recovered.world().find(ship)->hardpoint_requested_on[0];
            if(recovered.submit(wrong)){
                recovered.step();
                CHECK(recovered.world().events().back().result==Outcome::Unauthorized);
            }
            CHECK(recovered.world().find(ship)->hardpoint_requested_on[0]==original_intent);
            CHECK(recovered.world().total_material_kg()==
                  static_cast<std::int64_t>(expected_mass));
            CHECK(recovered.world().find(ship)->hull==100);
        }
        {
            PersistentWorld independently_recovered(copy,Simulation(10));
            CHECK(independently_recovered.world().canonical_state()==checkpoint);
            CHECK(independently_recovered.world().hardpoint_effective_on(ship,0));
            auto forged=checkpoint;
            const auto i=forged.find("shipstatus|");
            CHECK(i!=std::string::npos);
            auto end=forged.find('\n',i);
            CHECK(end!=std::string::npos);
            auto row=forged.substr(i,end-i);
            const auto key=row.find("|0|0|1");
            CHECK(key!=std::string::npos);
            row.replace(key,6,"|0|0|9");
            forged.replace(i,end-i,row);
            bool rejected=false;
            try{(void)Simulation::from_snapshot(forged);}
            catch(const std::runtime_error&){rejected=true;}
            CHECK(rejected);
        }
        fs::remove_all(dir);
        std::cout<<"PASS SHIP-STATUS-02: native module execution, v100 replay, rollback and original-world recovery\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL SHIP-STATUS-02 "<<e.what()<<'\n';
        std::error_code ignored;fs::remove_all(dir,ignored);return 1;
    }
}
