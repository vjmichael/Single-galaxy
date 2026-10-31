#include "galaxy/Navigation.hpp"
#include "galaxy/Persistence.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <cmath>
using namespace galaxy;
#define CHECK(x) do { if(!(x)) throw std::runtime_error(std::string("FAILED: ")+#x+" @ "+std::to_string(__LINE__)); }while(0)
static void travel(Vector2 station_b) {
    Simulation init(83);
    const auto a=init.add_station({0,0});
    const auto ship=init.spawn_ship_docked(7,a,20,100);
    const auto b=init.add_station(station_b);
    init.configure_market(a,2,1000);init.configure_market(b,5,1000);
    const auto db=std::filesystem::temp_directory_path()/
        ("galaxy-navigation-"+std::to_string(station_b.x)+"-"+std::to_string(station_b.y)+".sqlite");
    std::filesystem::remove(db);
    try {
      {
        PersistentWorld world(db.string(),std::move(init));
        std::uint64_t cmd=100;
        auto issue=[&](Action act,EntityId target=0,int x=0,int y=0){
            Command c{};c.id=++cmd;c.sequence=c.id;c.execute_tick=world.world().tick()+1;
            c.issuer=7;c.actor=ship;c.target=target;c.action=act;c.thrust_x=x;c.thrust_y=y;
            CHECK(world.submit(c));
            world.step();
            CHECK(world.world().events().back().result==Outcome::Applied);
        };
        issue(Action::RequestUndock,a);world.steps(2);
        CHECK(world.world().find(ship)->dock_state==DockState::InSpace);
        bool arrived=false;
        for(int i=0;i<90;++i){
            const auto& self=*world.world().find(ship);
            const auto nav=assist_to_station(self,station_b);
            if(nav.arrived){arrived=true;break;}
            issue(Action::Thrust,0,nav.thrust_x,nav.thrust_y);
        }
        CHECK(arrived);
        const auto& self=*world.world().find(ship);
        const auto dx=self.position.x-station_b.x, dy=self.position.y-station_b.y;
        CHECK(dx*dx+dy*dy<=Simulation::kDockingRangeMm*Simulation::kDockingRangeMm);
        CHECK(self.velocity.x==0 && self.velocity.y==0);
        issue(Action::RequestDock,b);world.steps(2);
        CHECK(world.world().find(ship)->dock_state==DockState::Docked);
        CHECK(world.world().find(ship)->dock_station==b);
        CHECK(world.world().total_ore()==0 && world.world().total_credits()==2000);
      }
      {
        // Saved ship and its docking station are recovered, without a phantom transfer.
        PersistentWorld restored(db.string(), Simulation(44));
        const auto* state=restored.world().find(ship);
        CHECK(state && state->dock_state==DockState::Docked && state->dock_station==b);
      }
      std::filesystem::remove(db);
    }catch(...){std::filesystem::remove(db);throw;}
}
int main(){try{
    travel({12000,7000});
    travel({0,14000});
    travel({-10000,-8000});
    Entity e{};e.type=EntityType::Ship;e.dock_state=DockState::Docked;
    bool rejected=false;try{(void)assist_to_station(e,{100,100});}catch(const std::invalid_argument&){rejected=true;}
    CHECK(rejected);
    std::cout<<"PASS B5.4 2-axis transport: diagonal, vertical, negative axes, real docking, recovery\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
