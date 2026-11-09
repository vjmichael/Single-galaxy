// FP3D-ENV-01 — portable integer swept collision and original-world replay.
#include "galaxy/NativeSweptCollision.hpp"
#include "galaxy/Persistence.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
using namespace galaxy::native_collision;
#define REQUIRE(x) do{if(!(x))throw std::runtime_error(std::string("FP3D-ENV-01 line ")+std::to_string(__LINE__)+": " #x);}while(false)
static void primitive_geometry(){
    REQUIRE(swept_ship_rock({10000,0,0},{20000,0,0},500)); // high speed across thin obstacle
    REQUIRE(swept_ship_rock({10000,1400,0},{20000,0,0},500)); // glancing 3D contact
    REQUIRE(!swept_ship_rock({10000,3000,0},{20000,0,0},500)); // near miss
    REQUIRE(!swept_ship_rock({10000,0,3000},{20000,0,0},500)); // true vertical separation
    REQUIRE(!swept_ship_rock({10000,0,0},{-10000,0,0},500)); // receding
    REQUIRE(!swept_ship_rock({10000,0,0},{0,0,0},500)); // no phantom static contact
    REQUIRE(swept_ship_rock({1000,0,0},{500,0,0},500)); // cannot advance deeper
    REQUIRE(!swept_ship_rock({1000,0,0},{-500,0,0},500)); // escape initial penetration
}
static Event send(PersistentWorld& world,std::uint64_t id,EntityId ship,int x){
    Command c{};
    c.id=id;c.sequence=id;c.execute_tick=world.world().tick()+1;
    c.issuer=7;c.actor=ship;c.action=Action::Thrust;c.thrust_x=x;
    REQUIRE(world.submit(c));
    auto old=world.world().events().size();
    world.step();
    for(std::size_t i=old;i<world.world().events().size();++i)
        if(world.world().events()[i].command_id==id)
            return world.world().events()[i];
    throw std::runtime_error("original STEP event absent");
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    primitive_geometry();
    const std::string path=argv[1];
    const auto cleanup=[&]{
        std::error_code ec;
        for(auto suffix:{"","-wal","-shm"})
            std::filesystem::remove(path+suffix,ec);
    };
    cleanup();
    try{
        Simulation seed(0x0E01);
        const auto ship=seed.add_colony_ship(7,{0,0},24,2);
        const auto rock=seed.add_geological_asteroid({25000,0},
            AsteroidClass::Metallic,10,900);
        seed.enable_native_galactic_physics({
            {1,galactic::Point{{0,100000},{0,100000},{0,100000}}}});
        seed.enable_native_swept_asteroid_collision();
        const auto initial_mass=seed.total_material_kg();
        std::uint64_t contact_tick=0;std::string expected_snapshot;
        {
            PersistentWorld world(path,std::move(seed));
            for(std::uint64_t id=1;id<=150;++id){
                REQUIRE(send(world,id,ship,1).result==Outcome::Applied);
                const auto& activity=world.world().activity_events();
                for(const auto& e:activity)
                    if(e.kind==ActivityKind::ShipAsteroidContact &&
                       e.actor==ship && e.target==rock){
                        contact_tick=e.tick;
                    }
                if(contact_tick)break;
            }
            REQUIRE(contact_tick>0);
            const auto* vessel=world.world().find(ship);
            REQUIRE(vessel && vessel->position.x<14000);
            REQUIRE(vessel->velocity.x==0 && vessel->velocity.y==0 &&
                    vessel->velocity_z_mm_per_tick==0);
            REQUIRE(vessel->hull==100);
            REQUIRE(world.world().find(rock)->ore>0);
            REQUIRE(world.world().total_material_kg()==initial_mass);
            const auto old_position=vessel->position.x;
            world.steps(4);
            REQUIRE(world.world().find(ship)->position.x==old_position);
            expected_snapshot=world.world().canonical_state();
        }
        {
            PersistentWorld world(path,Simulation(0x666));
            REQUIRE(world.world().canonical_state()==expected_snapshot);
            REQUIRE(world.world().find(ship)->hull==100);
            REQUIRE(world.world().find(rock)->ore>0);
            REQUIRE(world.world().total_material_kg()==initial_mass);
            bool has_real_contact=false;
            for(const auto& e:world.world().activity_events())
                if(e.kind==ActivityKind::ShipAsteroidContact &&
                   e.actor==ship && e.target==rock &&
                   e.tick==contact_tick && e.result==Outcome::Applied &&
                   e.transferred==0)has_real_contact=true;
            REQUIRE(has_real_contact);
        }
        cleanup();
        std::cout<<"FP3D-ENV-01 original native collision PASS: integer swept obstacles, fast/tangent/vertical/miss/escape, no rock penetration, bounded movement, real durable event, unchanged ore/hull/mass, SQLite restart\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';cleanup();return 1;}
}
