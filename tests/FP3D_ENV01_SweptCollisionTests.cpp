// FP3D-ENV-01 — portable integer swept collision and original-world replay.
#include "galaxy/NativeSweptCollision.hpp"
#include "galaxy/NativeStationWalkGeometry.hpp"
#include "galaxy/StationRefineryProtocol.hpp"
#include "galaxy/GalacticCoordinates.hpp"
#include <limits>
#include "galaxy/Persistence.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
using namespace galaxy::native_collision;
#define REQUIRE(x) do{if(!(x))throw std::runtime_error(std::string("FP3D-ENV-01 line ")+std::to_string(__LINE__)+": " #x);}while(false)
static void primitive_geometry(){
    // The entire 8m hull and wing span fit within the conservative 5.2m proxy.
    REQUIRE(kShipBoundingRadiusMm==5200);
    REQUIRE(swept_ship_rock({10000,0,0},{20000,0,0},500)); // high speed across thin obstacle
    REQUIRE(swept_ship_rock({10000,5500,0},{20000,0,0},500)); // glancing 3D contact
    REQUIRE(!swept_ship_rock({10000,7000,0},{20000,0,0},500)); // near miss
    REQUIRE(!swept_ship_rock({10000,0,7000},{20000,0,0},500)); // true vertical separation
    REQUIRE(!swept_ship_rock({10000,0,0},{-10000,0,0},500)); // receding
    REQUIRE(!swept_ship_rock({10000,0,0},{0,0,0},500)); // no phantom static contact
    REQUIRE(swept_ship_rock({1000,0,0},{500,0,0},500)); // cannot advance deeper
    REQUIRE(!swept_ship_rock({1000,0,0},{-500,0,0},500)); // escape initial penetration
    auto first=first_swept_ship_rock_contact({25000,0,0},{20000,0,0},10000);
    REQUIRE(first && first->first_contact_subtick>0 &&
            first->first_contact_subtick<kSubtickScale);
    REQUIRE(first->safe_delta.x>=9700 && first->safe_delta.x<9800);
    REQUIRE(first->safe_delta.y==0 && first->safe_delta.z==0);
    const auto radius=10000+kShipBoundingRadiusMm+kContactSkinMm;
    REQUIRE(25000-first->safe_delta.x>radius); // never land inside solid proxy
    const auto farther=first_swept_ship_rock_contact({35000,0,0},{20000,0,0},10000);
    REQUIRE(farther && farther->first_contact_subtick>first->first_contact_subtick);
    const auto adjacent=first_swept_ship_rock_contact({20000,0,0},{20000,0,0},10000);
    REQUIRE(adjacent && adjacent->first_contact_subtick<first->first_contact_subtick);
    // At an existing overlap, a ship can back away rather than be trapped.
    auto inward=first_swept_ship_rock_contact({1000,0,0},{20000,0,0},500);
    REQUIRE(inward && inward->first_contact_subtick==0 && inward->safe_delta.x==0);
}
static void high_order_seam_geometry(){
    // The 100km *render/partition* cell cannot be an invisible physics wall.
    using galaxy::galactic::Axis;
    using galaxy::galactic::Point;
    const auto edge=galaxy::galactic::kSectorEdgeMm;
    const auto cell=galaxy::galactic::kCellMm;
    const Point origin{{27,edge-10000},{-8,edge-5000},{3,cell-7000}};
    auto rock=galaxy::galactic::advance(origin,{25000,0,0});
    REQUIRE(rock && rock->x.sector==28 && origin.x.sector==27);
    auto q=galaxy::galactic::nearby_displacement(*rock,origin);
    REQUIRE(q && q->x_mm==25000 && q->y_mm==0 && q->z_mm==0);
    REQUIRE(swept_ship_rock({q->x_mm,q->y_mm,q->z_mm},{20000,0,0},10000));
    const auto moved=galaxy::galactic::advance(origin,{20000,0,0});
    REQUIRE(moved && moved->x.sector==28);
    auto reverse=galaxy::galactic::nearby_displacement(origin,*moved);
    REQUIRE(reverse && reverse->x_mm==-20000);
    const Point inner{{-12,cell-4000},{9,edge-3000},{2,edge-1000}};
    const auto diagonal=galaxy::galactic::advance(inner,{12000,6000,3000});
    REQUIRE(diagonal && diagonal->y.sector==10 &&
            diagonal->z.sector==3);
    const auto offset=galaxy::galactic::nearby_displacement(*diagonal,inner);
    REQUIRE(offset && offset->x_mm==12000 && offset->y_mm==6000 &&
            offset->z_mm==3000);
    REQUIRE(swept_ship_rock({offset->x_mm,offset->y_mm,offset->z_mm},
                            {20000,10000,5000},2500));
    REQUIRE(!swept_ship_rock({12000,6000,11000},{20000,10000,0},1000));
    // A distant sector is genuinely distant, never a modulo-local contact.
    const Point remote{{30,1000},{9,edge-3000},{2,edge-1000}};
    REQUIRE(!galaxy::galactic::nearby_displacement(remote,inner));
    const Point max_sect{{std::numeric_limits<std::int64_t>::max(),edge-100},
                          {0,0},{0,0}};
    REQUIRE(!galaxy::galactic::advance(max_sect,{200,0,0}));
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
static void real_station_collision_test(const std::string& save) {
    std::error_code ec;
    for(auto tail:{"","-wal","-shm"})std::filesystem::remove(save+tail,ec);
    REQUIRE(station_walk::clear(1600,0));
    REQUIRE(station_walk::clear(0,0));
    REQUIRE(station_walk::clear(2200,0));
    REQUIRE(!station_walk::clear(2300,0)); // native capsule vs solid crate
    REQUIRE(!station_walk::clear(-2300,-2500));
    REQUIRE(!station_walk::clear(0,-5000));
    REQUIRE(!station_walk::clear(-5000,0));
    REQUIRE(!station_walk::clear(5000,0));
    REQUIRE(!station_walk::clear(8100,0));
    EntityId station=0;
    {
        Simulation seed(0xE21);
        station=seed.add_station({0,0});
        (void)seed.spawn_ship_docked(7,station,12,24);
        seed.enable_native_galactic_physics({
            {1,galactic::Point{{0,100000},{0,100000},{0,100000}}}});
        seed.enable_native_swept_asteroid_collision();
        PersistentWorld world(save,std::move(seed));
        REQUIRE(world.register_player_at_station(7,station));
        std::uint64_t receipt=500;
        for(int step=0;step<6;++step)
            REQUIRE(world.station_player_action(7,station,0,
                StationPlayerAction::Walk,100,0,++receipt));
        REQUIRE(world.world().player_people().at(7).station_x_mm==2200);
        const auto prior=world.world().canonical_state();
        REQUIRE(!world.station_player_action(7,station,0,
            StationPlayerAction::Walk,100,0,++receipt));
        REQUIRE(world.world().canonical_state()==prior);
        for(int step=0;step<2;++step)
            REQUIRE(world.station_player_action(7,station,0,
                StationPlayerAction::Walk,-100,0,++receipt));
        REQUIRE(world.world().player_people().at(7).station_x_mm==2000);
        REQUIRE(station_refinery_protocol::accessible(
            world.world().player_people().at(7)));
    }
    {
        PersistentWorld recovered(save,Simulation(0xE21));
        REQUIRE(recovered.world().player_people().at(7).station_x_mm==2000);
        REQUIRE(recovered.world().player_people().at(7).physical_station==station);
    }
    for(auto tail:{"","-wal","-shm"})std::filesystem::remove(save+tail,ec);
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    primitive_geometry();
    high_order_seam_geometry();
    const std::string path=argv[1];
    real_station_collision_test(path+".walk");
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
        // The real rock and ship lie on OPPOSITE sector sides after
        // the 25m original genesis offset. This tests original SQLite motion,
        // not merely the standalone swept-geometry helper.
        seed.enable_native_galactic_physics({
            {1,galactic::Point{
                {27,galactic::kSectorEdgeMm-4000},
                {-8,galactic::kSectorEdgeMm-5000},
                {3,galactic::kCellMm-7000}}}});
        seed.enable_native_swept_asteroid_collision();
        const auto initial_mass=seed.total_material_kg();
        std::uint64_t contact_tick=0;std::string expected_snapshot;
        {
            PersistentWorld world(path,std::move(seed));
            for(std::uint64_t id=1;id<=150;++id){
                const auto before_position=world.world().find(ship)->position.x;
                REQUIRE(send(world,id,ship,1).result==Outcome::Applied);
                const auto& activity=world.world().activity_events();
                for(const auto& e:activity)
                    if(e.kind==ActivityKind::ShipAsteroidContact &&
                       e.actor==ship && e.target==rock){
                        contact_tick=e.tick;
                        // The last impact tick must move the hull up to its
                        // *actual* first safe millimetre, not freeze the
                        // entire 250..20,000mm step at its starting pose.
                        REQUIRE(world.world().find(ship)->position.x>before_position);
                    }
                if(contact_tick)break;
            }
            REQUIRE(contact_tick>0);
            const auto* vessel=world.world().find(ship);
            REQUIRE(vessel && vessel->position.x>9000 && vessel->position.x<10000);
            // The ship advances to the first safe contact millimetre,
            // rather than freezing at the beginning of its whole tick.
            REQUIRE(vessel->position.x>0);
            REQUIRE(vessel->physical_galactic_authoritative);
            REQUIRE(vessel->physical_galactic.x.sector==28);
            REQUIRE(world.world().find(rock)->physical_galactic.x.sector==28);
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
        std::cout<<"FP3D-ENV-01 original native collision, station geometry/terminal SQLite, inter-sector X/Y/Z seams and 100km cell crossings PASS: integer swept obstacles, fast/tangent/vertical/miss/escape, no rock penetration, bounded movement, real durable event, unchanged ore/hull/mass, SQLite restart\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';cleanup();return 1;}
}
