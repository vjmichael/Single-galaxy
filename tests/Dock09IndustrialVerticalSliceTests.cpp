// DOCK-09: original-space ore -> four original ship modules ->
// physically constructed outpost -> resident/local plant -> original worker ->
// finished 19kg panels; no synthetic factory inventory or worker creation.
// STAGED only: full native, CTest and installed tests run in consolidated pass.
#include "galaxy/ColonyOnboarding.hpp"
#include "galaxy/ColonyIndustrialService.hpp"
#include "galaxy/ColonyIndustrialManagement.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
namespace fs=std::filesystem;
using namespace galaxy;
#define CHECK(x) do{if(!(x))throw std::runtime_error("DOCK09 line "+std::to_string(__LINE__)+": " #x);}while(0)
static Event perform(PersistentWorld& w,std::uint64_t& seq,EntityId ship,
    Action action,EntityId target=0,std::int64_t amount=1,
    int x=0,int y=0) {
    Command c{};
    c.id=++seq;c.sequence=c.id;c.execute_tick=w.world().tick()+1;
    c.issuer=7;c.actor=ship;c.target=target;c.action=action;
    c.amount=amount;c.thrust_x=x;c.thrust_y=y;
    CHECK(w.submit(c));w.step();
    const auto result=w.world().events().back();
    CHECK(result.command_id==c.id&&result.result==Outcome::Applied);
    return result;
}
static Event select(PersistentWorld& w,std::uint64_t& seq,EntityId ship,
                    colony_onboarding::Operation action) {
    const auto c=colony_onboarding::propose(w.world(),7,ship,++seq,action);
    CHECK(c.has_value()&&c->id==seq&&c->actor==ship);
    CHECK(w.submit(*c));w.step();
    auto e=w.world().events().back();
    CHECK(e.command_id==seq&&e.result==Outcome::Applied);
    return e;
}
static Event manage(PersistentWorld& w,std::uint64_t& seq,
                    EntityId site,std::uint64_t selector) {
    const auto c=colony_management::propose(
        w.world(),7,site,selector,++seq);
    CHECK(c.has_value()&&c->target==site);
    CHECK(w.submit(*c));w.step();
    const auto e=w.world().events().back();
    CHECK(e.command_id==seq&&e.result==Outcome::Applied);
    return e;
}
static void erase(const std::string& path){
    for(const char* ext:{"","-wal","-shm"}){
        std::error_code ec;fs::remove(path+ext,ec);
    }
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    std::string db=argv[1];
    erase(db);
    try{
        EntityId home=0,station=0,raw=0,silicate=0,ship=0;
        std::uint64_t seq=92000;
        std::int64_t initial_mass=0;
        std::string stable;
        {
            Simulation seed(0xD009);
            home=seed.add_station({0,0});
            ship=seed.add_colony_ship(7,{6000,0},12,8);
            raw=seed.add_geological_asteroid(
                {8500,0},AsteroidClass::Metallic,64,2);
            silicate=seed.add_geological_asteroid(
                {8500,1400},AsteroidClass::Silicate,24,2);
            seed.provision_colony_genesis(ship,3000,3000,3000,3000,3000);
            seed.enable_individual_workforce();
            seed.enable_native_galactic_physics({
                {1,galactic::Point{{0,100000},{0,100000},{0,100000}}}});
            seed.enable_station_ore_provenance();
            seed.enable_native_swept_asteroid_collision();
            initial_mass=seed.total_material_kg();
            PersistentWorld w(db,std::move(seed));
            CHECK(w.world().find(ship)->dock_state==DockState::InSpace);
            CHECK(!colony_onboarding::propose(
                w.world(),8,ship,seq+1,
                colony_onboarding::Operation::RefineRawOre));
            CHECK(!colony_onboarding::propose(
                w.world(),7,home,seq+1,
                colony_onboarding::Operation::MakeModules));
            CHECK(!colony_onboarding::propose(
                w.world(),7,ship,seq+1,
                colony_onboarding::Operation::EstablishOutpost));
            // No starting material appears after read-only cockpit status.
            const auto before=w.world().state_hash();
            const auto initial_source=colony_onboarding::source_frame(
                w,7,ship,91,1);
            CHECK(initial_source.find("ONBOARD|1|91|1|")!=std::string::npos);
            CHECK(w.world().state_hash()==before);
            // Every source tonne is mined and every module paid for.
            // 20t ore -> 20t refined -> ten actual 2-tonne modules.
            for(int cycle=0;cycle<2;++cycle){
                perform(w,seq,ship,Action::Mine,raw,cycle==0?12:8);
                for(int k=0;k<(cycle==0?3:2);++k)
                    select(w,seq,ship,
                        colony_onboarding::Operation::RefineRawOre);
                for(int k=0;k<(cycle==0?3:2);++k)
                    select(w,seq,ship,
                        colony_onboarding::Operation::MakeModules);
            }
            CHECK(w.world().colony_ship(ship)->modules==10);
            CHECK(w.world().find(ship)->cargo==0);
            CHECK(w.world().total_material_kg()==initial_mass);
            // A separate real silicate rock yields original plagioclase,
            // not station-concentrate -> free workshop aluminum.
            perform(w,seq,ship,Action::Mine,silicate,1);
            const auto geological=select(w,seq,ship,
                colony_onboarding::Operation::SeparateAluminum);
            CHECK(geological.transferred>0);
            CHECK(w.world().ship_inventory_snapshot(ship).
                amount("good/aluminum")>=40);
            // Four original modules become embedded in one physical site,
            // deliberately outside the starting station collision radius.
            const auto founded=select(w,seq,ship,
                colony_onboarding::Operation::EstablishOutpost);
            CHECK(founded.transferred==8);
            station=colony_onboarding::constructed_site(w.world(),7,ship);
            CHECK(station && station!=home);
            CHECK(w.world().built_outposts().at(station).source_colony_ship==ship);
            CHECK(w.world().colony_ship(ship)->modules==6);
            CHECK(!colony_onboarding::propose(
                w.world(),8,ship,seq+1,
                colony_onboarding::Operation::EstablishOutpost));
            CHECK(w.world().total_material_kg()==initial_mass);
            // Native original docking comes BEFORE station person and I.
            perform(w,seq,ship,Action::RequestDock,station);
            w.steps(Simulation::kDockTransitionTicks);
            CHECK(w.world().find(ship)->dock_state==DockState::Docked);
            CHECK(w.world().find(ship)->dock_station==station);
            CHECK(w.register_player_at_station(7,station));
            CHECK(!colony_industry::local_resident(w.world(),7,home));
            CHECK(colony_industry::local_source_ship(
                w.world(),station,7)==ship);
            // The six remaining original modules embed in actual workshop,
            // warehouse, utility and habitat. No client-side construction.
            manage(w,seq,station,1);
            manage(w,seq,station,2);
            manage(w,seq,station,3);
            manage(w,seq,station,4);
            CHECK(w.world().colony_ship(ship)->modules==0);
            CHECK(w.world().workshop(station)&&w.world().storage(station));
            CHECK(w.world().utility(station)&&w.world().habitat(station));
            manage(w,seq,station,7);
            manage(w,seq,station,8);
            manage(w,seq,station,9);
            for(int n=0;n<3;++n)manage(w,seq,station,10);
            manage(w,seq,station,11);
            CHECK(w.world().habitat(station)->residents==3);
            // Source-backed ship -> warehouse -> workshop; repeat actual
            // shipments after real trade training consumes aluminum.
            perform(w,seq,ship,Action::TransferStorageStack,station,20,0,0);
            perform(w,seq,ship,Action::TransferStorageStack,station,20,0,3);
            CHECK(w.world().workshop(station)->aluminum_kg>=20);
            w.steps(12);
            auto original_workers=workforce02::plan_original_workshop(
                w.world(),7,ship,station);
            CHECK(original_workers[0].suggested_npc);
            const auto trainee=original_workers[0].suggested_npc;
            for(int n=0;n<3;++n)manage(w,seq,station,15);
            CHECK(w.world().workshop_trade_certified(
                trainee,WorkshopTrade::Cut));
            manage(w,seq,station,17);
            CHECK(w.world().workshop(station)->supervisor_npc_id==trainee);
            manage(w,seq,station,21);
            const auto* shop=w.world().workshop(station);
            CHECK(shop->operator_npc_id>0 &&
                  shop->operator_npc_id!=trainee);
            // Replenish native cut machine with another original 20kg,
            // never credit the workshop for its own purchased goods.
            perform(w,seq,ship,Action::TransferStorageStack,station,20,0,0);
            perform(w,seq,ship,Action::TransferStorageStack,station,20,0,3);
            const auto job=workforce02::approved_workshop_job(
                w.world(),7,ship,station,WorkshopTrade::Cut,1,++seq,true);
            CHECK(job && w.submit(*job));
            w.step();
            CHECK(w.world().events().back().command_id==seq);
            CHECK(w.world().events().back().result==Outcome::Applied);
            const auto before_batches=w.world().workshop(station)->completed_batches;
            const auto physical_before=w.world().total_material_kg();
            w.steps(16); // same bounded native world progression as INDRUN
            const auto* completed=w.world().workshop(station);
            CHECK(completed && completed->completed_batches==before_batches+1);
            CHECK(completed->panels_kg>=19);
            CHECK(w.world().total_material_kg()==physical_before);
            CHECK(w.world().total_material_kg()==initial_mass);
            CHECK(colony_industry::native_workshop_at(w.world(),station,7));
            stable=colony_industry::make_snapshot(w,7,91,12);
            CHECK(stable.find("|W|0|good/aluminum_panels|")!=
                  std::string::npos);
        }
        {
            PersistentWorld recovered(db,Simulation(0xD010));
            CHECK(recovered.world().player_people().at(7).physical_station==station);
            CHECK(recovered.world().workshop(station)->completed_batches>=1);
            CHECK(recovered.world().workshop(station)->panels_kg>=19);
            CHECK(recovered.world().total_material_kg()==initial_mass);
            CHECK(colony_industry::make_snapshot(recovered,7,91,12)==stable);
            CHECK(colony_onboarding::constructed_site(
                recovered.world(),8,ship)==0);
            CHECK(!colony_industry::local_source_ship(recovered.world(),home,7));
        }
        erase(db);
        std::cout<<"DOCK-09 staged actual outpost/materials/worker/product/restart PASS\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"DOCK-09 STAGED, NOT EXECUTED failure: "<<e.what()<<'\n';
        erase(db);
        return 1;
    }
}
