// PLAY-STATION-01: a finite, original-world game loop using an actual docked
// ship, native mining/haul/refinery commands, and real SQLite journal recovery.
// This is a NATIVE vertical slice: station-side player-person boarding and
// a separate station refinery are NOT yet implemented in the game kernel.
#include "galaxy/Persistence.hpp"
#include "galaxy/StationLaunch.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("PLAY-STATION-01 at ")+std::to_string(__LINE__)+": " #x);}while(false)

static Event execute(PersistentWorld& w, std::uint64_t& next, PlayerId owner,
                    EntityId ship, Action action, EntityId target=0,
                    std::int64_t amount=0, int thrust=0) {
    Command c{};c.id=++next;c.sequence=c.id;c.execute_tick=w.world().tick()+1;
    c.issuer=owner;c.actor=ship;c.action=action;c.target=target;
    c.amount=amount;c.thrust_x=thrust;
    CHECK(w.submit(c));w.step();
    const auto out=w.world().events().back();
    CHECK(out.command_id==c.id);return out;
}
static void geological_minerals_round_trip(const std::string& save) {
    Simulation seed(0x5303);
    const auto station=seed.add_station({0,0});
    const auto ship=seed.add_colony_ship(7,{1200,0},16,24);
    // A classified rock is not generic ore: mining a tonne sorts 1000 kg
    // of persistent named mineral phases into real ship compartments.
    const auto rock=seed.add_geological_asteroid({7000,0},
        AsteroidClass::Metallic,10,5000);
    CHECK(seed.register_player_person(7,ship));
    std::uint64_t serial=220000;
    const auto stock_before=seed.total_material_kg();
    {
        PersistentWorld w(save,std::move(seed));
        CHECK(execute(w,serial,7,ship,Action::RequestDock,station).result==Outcome::Applied);
        w.steps(2);
        CHECK(station_launch::available_ship(w.world(),7,station)==ship);
        CHECK(execute(w,serial,7,ship,Action::RequestUndock,station).result==Outcome::Applied);
        w.steps(2);
        CHECK(execute(w,serial,7,ship,Action::Thrust,0,0,1).result==Outcome::Applied);
        w.step();
        CHECK(execute(w,serial,7,ship,Action::Thrust,0,0,-1).result==Outcome::Applied);
        CHECK(w.world().find(ship)->velocity.x==0);
        const auto transfer=execute(w,serial,7,ship,Action::Mine,rock,1);
        CHECK(transfer.result==Outcome::Applied && transfer.transferred==1);
        const auto* colony=w.world().colony_ship(ship);
        CHECK(colony!=nullptr && colony->minerals_kg.at("nickel_iron")==860);
        CHECK(colony->minerals_kg.at("troilite")==80);
        CHECK(w.world().find(ship)->cargo==0); // not duplicated as bulk ore
        CHECK(w.world().total_material_kg()==stock_before);
        CHECK(execute(w,serial,7,ship,Action::RequestDock,station).result==Outcome::Applied);
        w.steps(2);
        CHECK(w.world().find(ship)->dock_state==DockState::Docked);
        // Onboard refinery while physically berthed. No station-side facility
        // is claimed: named 860kg concentrate becomes 774kg structural alloy,
        // 86kg tailings, while all other mineral fractions remain physical.
        const auto refine=execute(w,serial,7,ship,Action::RefineMineral,8,860);
        CHECK(refine.result==Outcome::Applied && refine.transferred==860);
        colony=w.world().colony_ship(ship);
        CHECK(colony->minerals_kg.count("nickel_iron")==0);
        CHECK(colony->goods_kg.at("structural_alloy")==774);
        CHECK(colony->tailings_kg==86);
        CHECK(w.world().total_material_kg()==stock_before);
        std::cout<<"PLAY-STATION-01 geology: mine exactly 1t rock -> 860kg nickel-iron + "
                    "140kg other mineral phases -> dock -> onboard refine 860kg into "
                    "774kg structural alloy + 86kg tailings PASS\n";
    }
    {
        PersistentWorld w(save,Simulation(333));
        CHECK(w.world().find(ship)->dock_state==DockState::Docked);
        CHECK(station_launch::available_ship(w.world(),7,station)==ship);
        CHECK(w.world().colony_ship(ship)->goods_kg.at("structural_alloy")==774);
        CHECK(w.world().colony_ship(ship)->tailings_kg==86);
        CHECK(w.world().total_material_kg()==stock_before);
        std::cout<<"PLAY-STATION-01 geology: named mineral refinement mass and player ship persistent after restart PASS\n";
    }
}

int main(int argc,char** argv) {
    if(argc!=2) {std::cerr<<"Usage: play_station_e2e SQLITE_PATH\n";return 2;}
    const fs::path db=argv[1];
    std::error_code ec;
    for(auto suffix:{"","-wal","-shm"})fs::remove(db.string()+suffix,ec);
    std::uint64_t id=120000;
    EntityId station=0,ship=0,asteroid=0;
    try {
        // A station and asteroid contain no magic free ship. A second owner's
        // ship is also NEVER available to the local player.
        {
            Simulation empty(100);
            const auto site=empty.add_station({0,0});
            CHECK(!station_launch::available_ship(empty,7,site));
            const auto outsider=empty.spawn_ship_docked(8,site,8,8);
            CHECK(outsider && !station_launch::available_ship(empty,7,site));
            CHECK(station_launch::available_ship(empty,8,site)==outsider);
        }
        Simulation seed(0x5301);
        station=seed.add_station({0,0});
        ship=seed.add_colony_ship(7,{1200,0},12,24);
        asteroid=seed.add_asteroid({7000,0},1); // precisely one tonne of raw/bulk ore
        CHECK(seed.register_player_person(7,ship)); // CURRENT LIMIT: player already aboard
        CHECK(!station_launch::available_ship(seed,7,station));
        {
            PersistentWorld w(db.string(),std::move(seed));
            auto result=execute(w,id,7,ship,Action::RequestDock,station);
            CHECK(result.result==Outcome::Applied);
            w.steps(2);
            CHECK(w.world().find(ship)->dock_state==DockState::Docked);
            CHECK(station_launch::available_ship(w.world(),7,station)==ship);
            // Player cannot mine an asteroid while parked in station.
            result=execute(w,id,7,ship,Action::Mine,asteroid,1);
            CHECK(result.result==Outcome::NotInSpace);
            // Owner cannot steal another player's vessel just by knowing the ID.
            result=execute(w,id,8,ship,Action::RequestUndock,station);
            CHECK(result.result==Outcome::Unauthorized);
            result=execute(w,id,7,ship,Action::RequestUndock,station);
            CHECK(result.result==Outcome::Applied);
            w.steps(2);
            CHECK(w.world().find(ship)->dock_state==DockState::InSpace);
            CHECK(!station_launch::available_ship(w.world(),7,station));
            // Actual C++ velocity, position and original Command receipts.
            const auto start=w.world().find(ship)->position.x;
            result=execute(w,id,7,ship,Action::Thrust,0,0,1);
            CHECK(result.result==Outcome::Applied);
            w.step();
            CHECK(w.world().find(ship)->position.x>start);
            result=execute(w,id,7,ship,Action::Thrust,0,0,-1);
            CHECK(result.result==Outcome::Applied);
            CHECK(w.world().find(ship)->velocity.x==0);
            CHECK(w.world().find(ship)->position.x<=Simulation::kDockingRangeMm);
            result=execute(w,id,7,ship,Action::Mine,asteroid,1);
            CHECK(result.result==Outcome::Applied && result.transferred==1);
            CHECK(w.world().find(ship)->cargo==1);
            CHECK(w.world().was_depleted(asteroid));
            CHECK(w.world().total_ore()==1);
            // One-shot mineral/ore harvest cannot repeat after depletion.
            result=execute(w,id,7,ship,Action::Mine,asteroid,1);
            CHECK(result.result==Outcome::NoResources && result.transferred==0);
            result=execute(w,id,7,ship,Action::RequestDock,station);
            CHECK(result.result==Outcome::Applied);
            w.steps(2);
            CHECK(w.world().find(ship)->dock_state==DockState::Docked);
            CHECK(station_launch::available_ship(w.world(),7,station)==ship);
            // Actual physical transfer from vessel to station bay (not teleport).
            result=execute(w,id,7,ship,Action::UnloadOre,station,1);
            CHECK(result.result==Outcome::Applied && result.transferred==1);
            CHECK(w.world().find(ship)->cargo==0);
            CHECK(w.world().hangar(station,7)->ore==1);
            CHECK(w.world().total_ore()==1);
            // Existing Refining is shipboard, so first load from real hangar.
            result=execute(w,id,7,ship,Action::RefineOre,0,1);
            CHECK(result.result==Outcome::NoResources);
            result=execute(w,id,7,ship,Action::LoadOre,station,1);
            CHECK(result.result==Outcome::Applied && result.transferred==1);
            CHECK(w.world().hangar(station,7)->ore==0);
            result=execute(w,id,7,ship,Action::RefineOre,0,1);
            CHECK(result.result==Outcome::Applied && result.transferred==1);
            CHECK(w.world().find(ship)->cargo==0);
            CHECK(w.world().colony_ship(ship)->refined==1);
            CHECK(w.world().total_ore()==1);
            std::cout<<"PLAY-STATION-01: station with available ship -> undock -> authentic flight -> "
                        "mine 1 tonne -> return dock -> unload in station hangar -> "
                        "reload into docked colony ship -> refine aboard -> durable refined=1 PASS\n";
        }
        {
            PersistentWorld reopened(db.string(),Simulation(0x5302));
            CHECK(reopened.world().find(ship)->dock_state==DockState::Docked);
            CHECK(station_launch::available_ship(reopened.world(),7,station)==ship);
            CHECK(reopened.world().find(ship)->cargo==0);
            CHECK(reopened.world().was_depleted(asteroid));
            CHECK(reopened.world().colony_ship(ship)->refined==1);
            CHECK(reopened.world().total_ore()==1);
            CHECK(reopened.world().player_people().at(7).physical_ship==ship);
            std::cout<<"PLAY-STATION-01: SQLite restart: station berth, exhausted asteroid, "
                        "exactly one refined ore-equivalent and player custody persisted PASS\n";
        }
        geological_minerals_round_trip(db.string()+".geological.sqlite");
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"PLAY-STATION-01 FAILED: "<<e.what()<<'\n';return 1;
    }
}
