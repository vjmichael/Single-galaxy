#include "galaxy/Persistence.hpp"
#include "galaxy/ColonyDefense.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("DEF01A: ")+#x+" line "+std::to_string(__LINE__));}while(false)
static void clean(const std::string& path){
    for(const auto* suffix:{"","-wal","-shm"})std::filesystem::remove(path+suffix);
}
static void sql(sqlite3* db,const std::string& code){
    char* err=nullptr;
    if(sqlite3_exec(db,code.c_str(),nullptr,nullptr,&err)!=SQLITE_OK){
        const std::string message=err?err:"unknown";
        sqlite3_free(err);throw std::runtime_error(message);
    }
}
int main(){
try{
    const auto file=(std::filesystem::temp_directory_path()/
        ("sg_watch_v81_"+std::to_string(std::chrono::steady_clock::now()
        .time_since_epoch().count())+".sqlite")).string();
    const auto copy=file+".recovered",bad=file+".tampered";
    EntityId source=0,friendly=0,foreign=0,outside=0;
    std::uint64_t original_generation=0;
    {
        Simulation sim(8101);
        source=sim.add_colony_ship(7,{0,0},100,5);
        friendly=sim.add_ship(7,{1500,0},100);
        foreign=sim.add_ship(8,{3000,0},100);
        outside=sim.add_ship(9,{25000,0},100);
        sim.provision_colony_genesis(source,1000,1000,1000,1000,1000);
        sim.enable_individual_workforce();
        sim.upgrade_assignment_snapshot_format();
        sim.enable_passenger_life_support();
        PersistentWorld world(file,std::move(sim));
        const auto initial=world.local_colony_watch_contacts(source,1);
        CHECK(initial.size()==1);
        CHECK(initial[0].foreign_ship==foreign);
        CHECK(initial[0].foreign_owner==8);
        CHECK(initial[0].distance_squared==9000000);
        CHECK(initial[0].world_tick==0);
        bool fake=false;
        try{(void)world.local_colony_watch_contacts(source,999999);}
        catch(const std::invalid_argument&){fake=true;}
        CHECK(fake);
        fake=false;
        try{(void)world.record_colony_watch_contact(8102,source,1,friendly);}
        catch(const std::runtime_error&){fake=true;}
        CHECK(fake);
        fake=false;
        try{(void)world.record_colony_watch_contact(8103,source,1,outside);}
        catch(const std::runtime_error&){fake=true;}
        CHECK(fake);
        world.steps(20);
        CHECK(world.world().tick()==20);
        original_generation=world.generation();
        const auto observed=world.record_colony_watch_contact(8101,source,1,foreign);
        CHECK(observed.world_tick==20);
        CHECK(observed.foreign_ship==foreign);
        CHECK(world.generation()==original_generation); // observation does not mutate world
        fake=false;
        try{(void)world.record_colony_watch_contact(8101,source,1,foreign);}
        catch(const std::runtime_error&){fake=true;}
        CHECK(fake);
        CHECK(world.world().entities().at(foreign).type==EntityType::Ship);
        CHECK(world.world().entities().at(outside).type==EntityType::Ship);
        world.steps(120-world.world().tick());
        CHECK(world.world().tick()==120);
    }
    // Restart independently replays original exact physical observer snapshot.
    {
        PersistentWorld replay(file,Simulation(8102));
        CHECK(replay.world().tick()==120);
        CHECK(replay.world().find(foreign)->owner_id==8);
        CHECK(replay.world().colony_ship(source)->crew==5);
        CHECK(replay.world().npc_records().size()==5);
        CHECK(replay.local_colony_watch_contacts(source,1).size()==1);
    }
    PersistentWorld::recover_copy(file,copy);
    {
        PersistentWorld replay(copy,Simulation(8103));
        CHECK(replay.world().tick()==120);
        CHECK(replay.local_colony_watch_contacts(source,1).size()==1);
    }
    PersistentWorld::recover_copy(file,bad);
    {
        sqlite3* raw=nullptr;
        CHECK(sqlite3_open(bad.c_str(),&raw)==SQLITE_OK);
        sql(raw,"DROP TRIGGER sg_colony_watch_no_update");
        sql(raw,"UPDATE sg_colony_watch_contacts SET foreign_owner='7' "
                "WHERE event_id=8101");
        sql(raw,"CREATE TRIGGER sg_colony_watch_no_update "
                "BEFORE UPDATE ON sg_colony_watch_contacts "
                "BEGIN SELECT RAISE(ABORT,'immutable local contact'); END");
        sqlite3_close(raw);
        bool refused=false;
        try{PersistentWorld forged(bad,Simulation(8104));}
        catch(const std::runtime_error&){refused=true;}
        CHECK(refused);
    }
    clean(file);clean(copy);clean(bad);
    std::cout<<"PASS DEF01A: 1 real foreign contact, friends/distant not flagged, "
        "no fabricated hostility, historical SQLite pin, restart and tamper rejection\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
