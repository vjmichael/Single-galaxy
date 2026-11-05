#include "galaxy/Persistence.hpp"
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do {if(!(x))throw std::runtime_error(std::string("SHIP-01B.1: ")+#x+" line "+std::to_string(__LINE__));}while(false)

static void fabricate(PersistentWorld& w,EntityId ship,EntityId rock,
                      std::uint64_t& command_id,std::int64_t batches) {
    auto work=[&](Action action,std::int64_t amount,EntityId target=0) {
        Command c{};
        c.id=++command_id;c.sequence=c.id;c.execute_tick=w.world().tick()+1;
        c.issuer=7;c.actor=ship;c.action=action;c.amount=amount;c.target=target;
        CHECK(w.submit(c));w.step();
        CHECK(w.world().events().back().result==Outcome::Applied);
    };
    for(std::int64_t i=0;i<batches;++i){
        work(Action::Mine,4,rock);
        work(Action::RefineOre,4);
        work(Action::FabricateModule,2);
    }
}
int main(){
    const auto db=fs::temp_directory_path()/
        ("single-galaxy-ship01b1-"+std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count())+".sqlite");
    EntityId colony=0,rock=0;
    std::array<std::uint64_t,2> workers{};
    std::uint64_t command_id=1000;
    std::int64_t original=0;
    std::string mid_build;
    try{
        {
            Simulation seed(881);
            colony=seed.add_colony_ship(7,{0,0},100,6);
            rock=seed.add_asteroid({2000,0},100);
            seed.provision_colony_genesis(colony,2000,2000,2000,2000,2000);
            seed.enable_individual_workforce();
            seed.ensure_physical_celestial_systems();
            seed.ensure_ship_frame_registry();
            for(const auto& [id,n]:seed.npc_records())
                if(n.home_ship==colony && !workers[0])workers[0]=id;
                else if(n.home_ship==colony && !workers[1])workers[1]=id;
            CHECK(workers[0] && workers[1] && workers[0]!=workers[1]);
            original=seed.total_ore();
            PersistentWorld w(db.string(),std::move(seed));
            CHECK(!w.queue_shipyard_freighter(7,91001,92001));
            fabricate(w,colony,rock,command_id,2); // 4 real modules
            CHECK(w.start_shipyard_construction(7,colony,91001,workers));
            w.steps(8);
            CHECK(w.world().shipyard_constructions().at(91001).operational());
            CHECK(w.world().colony_ship(colony)->modules==0);
            fabricate(w,colony,rock,command_id,3); // 6 more real modules
            CHECK(w.world().colony_ship(colony)->modules==6);
            CHECK(!w.queue_shipyard_freighter(8,91001,92001));
            CHECK(!w.queue_shipyard_freighter(7,91001,91001));
            const auto before=w.world().canonical_state();
            const auto generation=w.generation();
            bool failed=false;
            try{(void)w.queue_shipyard_freighter(7,91001,92001,
                FailPoint::AfterSnapshotWrite);}
            catch(const std::exception&){failed=true;}
            CHECK(failed);
            CHECK(w.world().canonical_state()==before);
            CHECK(w.generation()==generation);
            CHECK(w.world().colony_ship(colony)->modules==6);
            const auto entities_before=w.world().entities().size();
            CHECK(w.queue_shipyard_freighter(7,91001,92001));
            CHECK(!w.queue_shipyard_freighter(7,91001,92001));
            CHECK(!w.queue_shipyard_freighter(7,91001,92002)); // only one slip
            CHECK(w.world().colony_ship(colony)->modules==0);
            CHECK(w.world().total_ore()==original);
            const auto& queued=w.world().freighter_build_orders().at(92001);
            CHECK(queued.progress==0 && !queued.finished());
            CHECK(queued.embodied_ore==12);
            CHECK(w.world().entities().size()==entities_before);
            CHECK(w.world().canonical_state().rfind("galaxy-kernel-v91|",0)==0);
            w.steps(5);
            CHECK(w.world().freighter_build_orders().at(92001).progress==5);
            CHECK(w.world().freighter_build_orders().at(92001).launched_ship==0);
            CHECK(w.world().entities().size()==entities_before);
            CHECK(w.world().total_ore()==original);
            mid_build=w.world().canonical_state();
            CHECK(Simulation::from_snapshot(mid_build).canonical_state()==mid_build);
        }
        {
            // Real SQLite reopen, not a memory copy: same unfinished ship
            // build and material provenance, no early extra hull or NPC.
            PersistentWorld w(db.string(),Simulation(881));
            CHECK(w.world().canonical_state()==mid_build);
            CHECK(w.world().freighter_build_orders().at(92001).progress==5);
            const auto before=w.world().entities().size();
            const auto npcs=w.world().npc_records().size();
            w.steps(6);
            CHECK(w.world().freighter_build_orders().at(92001).progress==11);
            CHECK(w.world().entities().size()==before);
            const auto snapshot=w.world().canonical_state();
            const auto generation=w.generation();
            bool fail=false;
            try{w.step(FailPoint::AfterSnapshotWrite);}
            catch(const std::exception&){fail=true;}
            CHECK(fail && w.generation()==generation);
            CHECK(w.world().canonical_state()==snapshot);
            CHECK(w.world().entities().size()==before);
            w.step(); // ONLY this committed twelfth work tick mints the hull
            const auto& result=w.world().freighter_build_orders().at(92001);
            CHECK(result.finished());
            CHECK(result.progress==12);
            CHECK(result.launched_tick==w.world().tick());
            CHECK(result.launched_ship!=0);
            const auto* vessel=w.world().find(result.launched_ship);
            const auto* source=w.world().find(colony);
            CHECK(vessel && source && vessel->id!=source->id);
            CHECK(vessel->type==EntityType::Ship);
            CHECK(vessel->owner_id==7);
            CHECK(vessel->cargo_capacity==100);
            CHECK(vessel->hull==100);
            CHECK(vessel->ore==0 && vessel->cargo==0);
            CHECK(vessel->weapon_charges==0);
            CHECK(!vessel->fitting.exploration_scanner());
            CHECK(!vessel->fitting.probe_launcher());
            CHECK(w.world().colony_ship(vessel->id)==nullptr); // no free crew
            CHECK(w.world().life_support(vessel->id)==nullptr);
            CHECK(w.world().npc_records().size()==npcs);
            CHECK(w.world().ship_frames().at(vessel->id).system==
                  w.world().ship_frames().at(colony).system);
            CHECK(w.world().ship_frames().at(vessel->id).anchor_body==
                  w.world().ship_frames().at(colony).anchor_body);
            CHECK(vessel->system_id==source->system_id);
            CHECK(vessel->position.y==source->position.y);
            CHECK(std::abs(vessel->position.x-source->position.x)<=1000);
            CHECK(w.world().total_ore()==original);
            CHECK(w.world().entities().size()==before+1);
            CHECK(!w.queue_shipyard_freighter(7,91001,92001));
            const auto built=w.world().canonical_state();
            CHECK(Simulation::from_snapshot(built).canonical_state()==built);
            auto forged=built;
            const auto at=forged.find("freighterbuild|92001|");
            CHECK(at!=std::string::npos);
            const auto end=forged.find('\n',at);
            CHECK(end!=std::string::npos);
            auto fields=forged.substr(at,end-at);
            const auto id_txt="|"+std::to_string(result.launched_ship)+"|";
            const auto found=fields.find(id_txt);
            CHECK(found!=std::string::npos);
            fields.replace(found+1,id_txt.size()-2,"999999999");
            forged.replace(at,end-at,fields);
            bool rejected=false;
            try{(void)Simulation::from_snapshot(forged);}
            catch(const std::exception&){rejected=true;}
            CHECK(rejected);
            w.steps(3);
            CHECK(w.world().entities().size()==before+1);
            CHECK(w.world().freighter_build_orders().at(92001).launched_ship==
                  result.launched_ship);
            CHECK(w.world().total_ore()==original);
        }
        {
            PersistentWorld w(db.string(),Simulation(881));
            const auto& order=w.world().freighter_build_orders().at(92001);
            CHECK(order.finished());
            CHECK(w.world().find(order.launched_ship)!=nullptr);
            CHECK(w.world().total_ore()==original);
        }
        fs::remove(db);
        std::cout<<"PASS SHIP-01B.1: first physical freighter, finite material "
                    "build, same-frame ship entity, single mint, SQLite recovery\n";
    }catch(...){fs::remove(db);throw;}
}
