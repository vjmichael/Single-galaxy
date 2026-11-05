#include "galaxy/Persistence.hpp"
#include "galaxy/ShipyardConstructionPreflight.hpp"
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("SHIP01A.1: ")+#x+" line "+std::to_string(__LINE__));}while(false)

static void fabricate(PersistentWorld& w,EntityId colony,EntityId asteroid){
    const auto issue=[&](std::uint64_t id,Action action,std::int64_t kg,EntityId target=0){
        Command c{};
        c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
        c.issuer=7;c.actor=colony;c.action=action;c.amount=kg;c.target=target;
        CHECK(w.submit(c));w.step();
        CHECK(w.world().events().back().result==Outcome::Applied);
    };
    issue(1001,Action::Mine,4,asteroid);
    issue(1002,Action::RefineOre,4);
    issue(1003,Action::FabricateModule,2);
    issue(1004,Action::Mine,4,asteroid);
    issue(1005,Action::RefineOre,4);
    issue(1006,Action::FabricateModule,2);
}
int main(){
    const auto db=fs::temp_directory_path()/
        ("single-galaxy-ship01a1-"+std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count())+".sqlite");
    EntityId ship=0;
    std::array<std::uint64_t,2> workers{};
    std::int64_t original=0;
    std::uint64_t before_restart_tick=0;
    std::string committed;
    try{
        {
            Simulation seed(1207);
            ship=seed.add_colony_ship(7,{0,0},60,6);
            const auto asteroid=seed.add_asteroid({2000,0},40);
            seed.provision_colony_genesis(ship,1500,1500,1500,1500,1500);
            seed.enable_individual_workforce();
            seed.ensure_physical_celestial_systems();
            seed.ensure_ship_frame_registry();
            for(const auto& [id,n]:seed.npc_records())
                if(n.home_ship==ship && !workers[0])workers[0]=id;
                else if(n.home_ship==ship && !workers[1])workers[1]=id;
            CHECK(workers[0] && workers[1] && workers[0]!=workers[1]);
            original=seed.total_ore();
            PersistentWorld w(db.string(),std::move(seed));
            CHECK(!w.start_shipyard_construction(8,ship,99001,workers));
            CHECK(!w.start_shipyard_construction(7,ship,99001,
                {{workers[0],workers[0]}}));
            CHECK(!w.start_shipyard_construction(7,ship,99001,workers));
            fabricate(w,ship,asteroid);
            CHECK(w.world().colony_ship(ship)->modules==4);
            const auto before=w.world().canonical_state();
            const auto gen=w.generation();
            bool injected=false;
            try{(void)w.start_shipyard_construction(7,ship,99001,workers,
                FailPoint::AfterSnapshotWrite);}
            catch(const std::exception&){injected=true;}
            CHECK(injected);
            CHECK(w.world().canonical_state()==before);
            CHECK(w.generation()==gen);
            CHECK(w.world().colony_ship(ship)->modules==4);
            CHECK(w.world().shipyard_constructions().empty());
            CHECK(w.start_shipyard_construction(7,ship,99001,workers));
            CHECK(!w.start_shipyard_construction(7,ship,99001,workers));
            CHECK(!w.start_shipyard_construction(7,ship,99002,workers));
            CHECK(w.world().colony_ship(ship)->modules==0);
            CHECK(w.world().total_ore()==original);
            const auto& begun=w.world().shipyard_constructions().at(99001);
            CHECK(begun.embodied_ore==8);
            CHECK(begun.required==8);
            CHECK(begun.progress==0);
            CHECK(!begun.operational());
            CHECK(w.world().canonical_state().rfind("galaxy-kernel-v90|",0)==0);
            // A rejected atomic world step must never increment labor or
            // publish progress if its SQLite snapshot fails.
            const auto prior_step=w.world().canonical_state();
            const auto prior_generation=w.generation();
            bool step_failed=false;
            try{w.step(FailPoint::AfterSnapshotWrite);}
            catch(const std::exception&){step_failed=true;}
            CHECK(step_failed);
            CHECK(w.world().canonical_state()==prior_step);
            CHECK(w.generation()==prior_generation);
            w.steps(3);
            CHECK(w.world().shipyard_constructions().at(99001).progress==3);
            CHECK(w.world().total_ore()==original);
            before_restart_tick=w.world().tick();
            committed=w.world().canonical_state();
            CHECK(Simulation::from_snapshot(committed).canonical_state()==committed);
            // Snapshot cannot erase physical embodied material.
            auto forged=committed;
            const auto pos=forged.find("shipyard|99001|");
            CHECK(pos!=std::string::npos);
            const auto end=forged.find('\n',pos);
            CHECK(end!=std::string::npos);
            const auto old=forged.substr(pos,end-pos);
            const auto embed=old.find("|8|3|8|");
            CHECK(embed!=std::string::npos);
            forged.replace(pos+embed+1,1,"9");
            bool invalid=false;
            try{(void)Simulation::from_snapshot(forged);}
            catch(const std::exception&){invalid=true;}
            CHECK(invalid);
        }
        {
            PersistentWorld w(db.string(),Simulation(1207));
            CHECK(w.world().canonical_state()==committed);
            CHECK(w.world().tick()==before_restart_tick);
            CHECK(w.world().shipyard_constructions().at(99001).progress==3);
            CHECK(w.world().colony_ship(ship)->modules==0);
            w.steps(5);
            const auto& finished=w.world().shipyard_constructions().at(99001);
            CHECK(finished.operational());
            CHECK(finished.progress==8);
            CHECK(finished.last_progress_tick==w.world().tick());
            CHECK(w.world().total_ore()==original);
            const auto end=w.world().canonical_state();
            w.steps(2);
            CHECK(w.world().shipyard_constructions().at(99001).progress==8);
            CHECK(w.world().total_ore()==original);
            CHECK(Simulation::from_snapshot(end).canonical_state()==end);
        }
        fs::remove(db);
        std::cout<<"PASS SHIP-01A.1: real module debit, named workforce, v90 state, "
                    "SQLite rollback, restart, exact-once completed yard\n";
        return 0;
    }catch(...){
        fs::remove(db);
        throw;
    }
}
