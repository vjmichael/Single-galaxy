// Genuine original-world, SQLite-backed one-tonne mining acceptance.
#include "galaxy/Persistence.hpp"
#include "galaxy/SpaceMiningContact.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("04B line ")+std::to_string(__LINE__)+": " #x);}while(false)
static Event run(PersistentWorld& w,std::uint64_t& id,PlayerId owner,
                 EntityId ship,Action action,EntityId target=0,
                 std::int64_t amount=0,int thrust=0) {
    Command cmd{};
    cmd.id=++id;cmd.sequence=id;cmd.execute_tick=w.world().tick()+1;
    cmd.issuer=owner;cmd.actor=ship;cmd.action=action;cmd.target=target;
    cmd.amount=amount;cmd.thrust_x=thrust;
    CHECK(w.submit(cmd));
    auto before=w.world().events().size();
    w.step();
    for(std::size_t i=before;i<w.world().events().size();++i)
        if(w.world().events()[i].command_id==id)return w.world().events()[i];
    throw std::runtime_error("04B native command receipt missing");
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    auto path=std::string(argv[1]);
    const auto clean=[&]{std::error_code e;for(auto s:{"","-wal","-shm"})std::filesystem::remove(path+s,e);};
    clean();
    try{
        Simulation seed(0x404B);
        const auto station=seed.add_station({0,0});
        const auto ship=seed.add_colony_ship(7,{0,0},24,8);
        const auto other=seed.add_colony_ship(8,{60000,0},8,2);
        (void)other;
        const auto rock=seed.add_geological_asteroid({25000,0},AsteroidClass::Metallic,10,900);
        seed.enable_native_galactic_physics({{1,galactic::Point{
            {0,100000},{0,100000},{0,100000}}}});
        seed.enable_station_ore_provenance();
        seed.enable_ship_mining_equipment(); // opt-in native v126; no auto grant
        CHECK(!seed.find(ship)->fitting.mining_tool());
        seed.provision_mining_test_refined_stock(ship); // finite initial 2t, no drill
        CHECK(seed.register_player_person(7,ship));
        const auto original_mass=seed.total_material_kg();
        const auto original_rock=seed.find(rock)->ore;
        std::uint64_t id=81000;
        {
            PersistentWorld w(path,std::move(seed));
            CHECK(run(w,id,7,ship,Action::Mine,rock,1).result==Outcome::ModuleOffline);
            CHECK(run(w,id,7,ship,Action::ConfigureMiningTool,0,1).result==Outcome::NotDocked);
            CHECK(run(w,id,7,ship,Action::RequestDock,station).result==Outcome::Applied);
            w.steps(Simulation::kDockTransitionTicks);
            CHECK(w.world().find(ship)->dock_state==DockState::Docked);
            CHECK(run(w,id,7,ship,Action::Mine,rock,1).result==Outcome::NotInSpace);
            CHECK(run(w,id,7,ship,Action::ConfigureMiningTool,0,1).result==Outcome::NoResources);
            CHECK(run(w,id,7,ship,Action::FabricateModule,0,1).result==Outcome::Applied);
            CHECK(w.world().colony_ship(ship)->refined==0);
            CHECK(w.world().colony_ship(ship)->modules==1);
            auto fit=run(w,id,7,ship,Action::ConfigureMiningTool,0,1);
            CHECK(fit.result==Outcome::Applied && fit.transferred==2);
            CHECK(w.world().find(ship)->fitting.mining_tool());
            CHECK(w.world().colony_ship(ship)->modules==0);
            CHECK(w.world().total_material_kg()==original_mass);
            CHECK(run(w,id,7,ship,Action::ConfigureMiningTool,0,1).result==Outcome::InvalidArgument);
            CHECK(run(w,id,7,ship,Action::SetHardpointPower,0,0).result==Outcome::Applied);
            CHECK(!space_mining_view::tool_online(w.world(),*w.world().find(ship)));
            CHECK(run(w,id,7,ship,Action::RequestUndock,station).result==Outcome::Applied);
            w.steps(Simulation::kDockTransitionTicks);
            CHECK(w.world().find(ship)->dock_state==DockState::InSpace);
            CHECK(run(w,id,7,ship,Action::Mine,rock,1).result==Outcome::ModuleOffline);
            CHECK(run(w,id,7,ship,Action::SetHardpointPower,0,1).result==Outcome::Applied);
            CHECK(run(w,id,7,ship,Action::Mine,rock,1).result==Outcome::OutOfRange);
            CHECK(run(w,id,8,ship,Action::Mine,rock,1).result==Outcome::Unauthorized);
            CHECK(run(w,id,7,ship,Action::Mine,rock,2).result==Outcome::InvalidArgument);
            CHECK(run(w,id,7,ship,Action::StartMining,rock,1).result==Outcome::InvalidArgument);
            CHECK(w.world().find(rock)->ore==original_rock && w.world().find(ship)->cargo==0);
            CHECK(run(w,id,7,ship,Action::Thrust,0,0,1).result==Outcome::Applied);
            w.steps(42);
            auto contact=space_mining_view::nearest_original_asteroid(w.world(),7,ship);
            CHECK(contact && contact->rock==rock && contact->x_mm>0 && contact->x_mm<15000);
            const auto mine_id=id+1;
            auto ore=run(w,id,7,ship,Action::Mine,rock,1);
            CHECK(ore.result==Outcome::Applied && ore.transferred==1);
            CHECK(w.world().find(rock)->ore==original_rock-1);
            CHECK(w.world().find(ship)->cargo==1);
            CHECK(w.world().station_ore_lots().size()==1);
            const auto& lot=w.world().station_ore_lots().begin()->second;
            CHECK(lot.rock==rock && lot.owner==7 && lot.source_ship==ship &&
                  lot.station==0 && lot.taxonomy==2 && lot.tonnes==1 &&
                  lot.mine_command==mine_id);
            CHECK(w.world().total_material_kg()==original_mass);
            Command replay{};
            replay.id=mine_id;replay.sequence=mine_id;
            replay.execute_tick=w.world().tick()+1;replay.issuer=7;
            replay.actor=ship;replay.target=rock;replay.amount=1;
            replay.action=Action::Mine;
            CHECK(!w.submit(replay)); // unchanged durable command ID
            CHECK(w.world().find(rock)->ore==original_rock-1);
            CHECK(run(w,id,7,ship,Action::SetHardpointPower,0,0).result==Outcome::Applied);
            CHECK(run(w,id,7,ship,Action::Mine,rock,1).result==Outcome::ModuleOffline);
        }
        {
            PersistentWorld w(path,Simulation(0x543));
            CHECK(w.world().find(rock)->ore==original_rock-1);
            CHECK(w.world().find(ship)->cargo==1);
            CHECK(w.world().find(ship)->fitting.mining_tool());
            CHECK(!space_mining_view::tool_online(w.world(),*w.world().find(ship)));
            CHECK(w.world().station_ore_lots().size()==1);
            CHECK(w.world().total_material_kg()==original_mass);
            CHECK(run(w,id,7,ship,Action::Mine,rock,1).result==Outcome::ModuleOffline);
        }
        clean();
        std::cout<<"PLAY-STATION-04B PASS: real finite 2t module, drill fit/power, source distance, owner, no unapproved auto-mining, exact 1t geological provenance and cargo, replay, SQLite restart\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';clean();return 1;}
}
