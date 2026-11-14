#include "galaxy/Navigation.hpp"
#include <stdexcept>
#include <iostream>
using namespace galaxy;
#define CHECK(x) do { if(!(x)) throw std::runtime_error(#x); }while(false)
static Entity object(EntityId id,EntityType type,Vector2 pos,Vector2 vel={}) {
    Entity e{}; e.id=id; e.type=type;e.position=pos;e.velocity=vel;
    e.system_id=1;return e;
}
int main(){
    try {
        auto ship=object(1,EntityType::Ship,{0,0});
        auto asteroid=object(2,EntityType::Asteroid,{10000,0});
        auto dock=object(3,EntityType::Station,{10000,0});
        auto wreck=object(4,EntityType::Wreck,{0,-10000});
        auto freighter=object(5,EntityType::Ship,{10000,3000},{250,0});
        const auto mine=assist_to_object(ship,asteroid,5000);
        CHECK(!mine.arrived && mine.waypoint.x==5000);
        CHECK(mine.thrust_x==1 && mine.thrust_y==0);
        CHECK(assist_to_object(ship,dock,1500).waypoint.x==8500);
        CHECK(assist_to_station(ship,dock.position).waypoint.x==8500);
        CHECK(assist_to_object(ship,wreck,2500).waypoint.y==-7500);
        const auto moving=assist_to_object(ship,freighter,1500);
        CHECK(!moving.arrived && moving.waypoint.x==8500);
        CHECK(moving.thrust_x==1);
        // Close to a moving ship is not arrival until velocities match.
        ship.position=moving.waypoint;
        CHECK(!assist_to_object(ship,freighter,1500).arrived);
        ship.velocity=freighter.velocity;
        CHECK(assist_to_object(ship,freighter,1500).arrived);
        // Moving target changes its actual position: no stale waypoint.
        freighter.position.x+=500; // clear the 250 mm arrival tolerance
        CHECK(!assist_to_object(ship,freighter,1500).arrived);
        // Never authorize arbitrary systems or self-approach.
        bool rejected=false;
        try{(void)assist_to_object(ship,ship,1000);}
        catch(const std::invalid_argument&){rejected=true;}
        CHECK(rejected);
        asteroid.system_id=2;rejected=false;
        try{(void)assist_to_object(ship,asteroid,1000);}
        catch(const std::invalid_argument&){rejected=true;}
        CHECK(rejected);
        std::cout<<"PASS FLIGHT-03 target-neutral guidance\n";
    }catch(const std::exception& e){
        std::cerr<<"FAIL FLIGHT-03: "<<e.what()<<'\n';return 1;
    }
}
