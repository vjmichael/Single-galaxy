#include "galaxy/Persistence.hpp"
#include "galaxy/ColonySafety.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
#define CHECK(v) do{if(!(v))throw std::runtime_error(std::string("AUTO04D5C1: ")+ #v+" line "+std::to_string(__LINE__));}while(false)
static Event command(PersistentWorld& w,EntityId ship,std::uint64_t id,Action action,
                     EntityId target=0,std::int64_t amount=1,int selector=0){
    Command c{};c.id=id;c.sequence=id;c.issuer=7;
    c.actor=ship;c.action=action;c.target=target;c.amount=amount;
    c.thrust_x=selector;c.execute_tick=w.world().tick()+1;
    CHECK(w.submit(c));w.step();
    return w.world().events().back();
}
static void clean(const std::string& path){
    for(const auto* suffix:{"","-wal","-shm"})std::filesystem::remove(path+suffix);
}
static void sql(sqlite3* db,const std::string& code){
    char* err=nullptr;
    if(sqlite3_exec(db,code.c_str(),nullptr,nullptr,&err)!=SQLITE_OK){
        const std::string message=err?err:"unknown";
        sqlite3_free(err);throw std::runtime_error("tamper SQL "+message);
    }
}
int main(){
try{
    const auto db=(std::filesystem::temp_directory_path()/
        ("sg_npc_supply_v80_"+std::to_string(std::chrono::steady_clock::now()
        .time_since_epoch().count())+".sqlite")).string();
    const auto recovered=db+".recovered",tampered=db+".tampered";
    EntityId home=0,habitat=0;
    constexpr std::uint64_t supply_command=8050;
    std::int64_t mass=0,source_before=0,habitat_before=0,amount=0;
    {
        Simulation sim(8050);
        home=sim.add_colony_ship(7,{0,0},100,24);
        sim.provision_colony_genesis(home,1000,3000,1000,1000,1000);
        const auto rock=sim.add_asteroid({2000,0},70);
        sim.enable_individual_workforce();
        sim.upgrade_assignment_snapshot_format();
        sim.enable_passenger_life_support();
        mass=sim.total_material_kg();
        PersistentWorld w(db,std::move(sim));
        CHECK(command(w,home,1,Action::Mine,rock,60).result==Outcome::Applied);
        CHECK(command(w,home,2,Action::RefineOre,0,60).result==Outcome::Applied);
        CHECK(command(w,home,3,Action::FabricateModule,0,30).result==Outcome::Applied);
        CHECK(command(w,home,4,Action::ConstructOutpost).result==Outcome::Applied);
        habitat=w.world().built_outposts().begin()->first;
        CHECK(command(w,home,5,Action::ConstructHabitat,habitat).result==Outcome::Applied);
        CHECK(command(w,home,6,Action::TransferHabitatStock,habitat,25,1).result==Outcome::Applied);
        CHECK(command(w,home,7,Action::TransferHabitatStock,habitat,100,2).result==Outcome::Applied);
        CHECK(command(w,home,8,Action::TransferHabitatStock,habitat,100,3).result==Outcome::Applied);
        CHECK(command(w,home,9,Action::AssignHabitatResidents,habitat,3).result==Outcome::Applied);
        CHECK(w.world().habitat(habitat)->residents==3);
        const auto safe=w.supervise_npc_habitat_resupply(home,habitat,supply_command);
        CHECK(safe.status==PersistentWorld::NpcLocalSupplyStatus::NoEmergency);
        CHECK(!w.verified_npc_habitat_resupply(supply_command));
        w.steps(140-w.world().tick());
        const auto physical=colony_safety::assess(w.world(),home,habitat);
        CHECK(physical.risk==colony_safety::Risk::Critical);
        CHECK(physical.population==3);
        source_before=w.world().life_support(home)->food_kg;
        habitat_before=w.world().habitat(habitat)->life.food_kg;
        CHECK(habitat_before==4);
        CHECK(w.world().total_material_kg()==mass);
        const auto generation=w.generation();
        bool injected=false;
        try{(void)w.supervise_npc_habitat_resupply(
            home,habitat,supply_command,FailPoint::AfterSnapshotWrite);}
        catch(const std::runtime_error&){injected=true;}
        CHECK(injected);
        CHECK(w.generation()==generation);
        CHECK(!w.world().has_command_id(supply_command));
        CHECK(w.world().life_support(home)->food_kg==source_before);
        CHECK(w.world().habitat(habitat)->life.food_kg==habitat_before);
        const auto accepted=w.supervise_npc_habitat_resupply(home,habitat,supply_command);
        CHECK(accepted.status==PersistentWorld::NpcLocalSupplyStatus::Accepted);
        CHECK(accepted.npc_id>0);
        CHECK(accepted.notice_id>0);
        CHECK(accepted.resource_selector==1);
        CHECK(accepted.amount_kg==8);
        amount=accepted.amount_kg;
        CHECK(w.generation()==generation+1);
        CHECK(w.world().life_support(home)->food_kg==source_before);
        CHECK(w.world().habitat(habitat)->life.food_kg==habitat_before);
        CHECK(!w.verified_npc_habitat_resupply(supply_command));
        const auto blocked=w.supervise_npc_habitat_resupply(home,habitat,supply_command+1);
        CHECK(blocked.status==PersistentWorld::NpcLocalSupplyStatus::NoUsableStock);
        CHECK(w.world().total_material_kg()==mass);
    }
    {
        PersistentWorld w(db,Simulation(8051));
        CHECK(w.world().has_command_id(supply_command));
        CHECK(!w.verified_npc_habitat_resupply(supply_command));
        bool injected=false;
        try{w.step(FailPoint::AfterSnapshotWrite);}
        catch(const std::runtime_error&){injected=true;}
        CHECK(injected);
        CHECK(w.world().life_support(home)->food_kg==source_before);
        CHECK(w.world().habitat(habitat)->life.food_kg==habitat_before);
        CHECK(!w.verified_npc_habitat_resupply(supply_command));
    }
    {
        PersistentWorld w(db,Simulation(8052));
        CHECK(w.world().has_command_id(supply_command));
        w.step();
        CHECK(w.world().events().back().command_id==supply_command);
        CHECK(w.world().events().back().result==Outcome::Applied);
        CHECK(w.world().events().back().transferred==amount);
        CHECK(w.world().life_support(home)->food_kg==source_before-amount);
        CHECK(w.world().habitat(habitat)->life.food_kg==habitat_before+amount);
        CHECK(w.world().total_material_kg()==mass);
        CHECK(w.verified_npc_habitat_resupply(supply_command));
        w.steps(200-w.world().tick());
        CHECK(w.verified_npc_habitat_resupply(supply_command));
        CHECK(w.world().total_material_kg()==mass);
    }
    {
        PersistentWorld w(db,Simulation(8053));
        CHECK(w.verified_npc_habitat_resupply(supply_command));
        CHECK(w.world().total_material_kg()==mass);
    }
    PersistentWorld::recover_copy(db,recovered);
    {
        PersistentWorld w(recovered,Simulation(8054));
        CHECK(w.verified_npc_habitat_resupply(supply_command));
    }
    PersistentWorld::recover_copy(db,tampered);
    {
        sqlite3* raw=nullptr;
        CHECK(sqlite3_open(tampered.c_str(),&raw)==SQLITE_OK);
        sql(raw,"DROP TRIGGER sg_colony_npc_emergency_supply_no_update");
        sql(raw,"UPDATE sg_colony_npc_emergency_supply SET npc_id='999999' "
            "WHERE command_id='8050'");
        sql(raw,"CREATE TRIGGER sg_colony_npc_emergency_supply_no_update "
            "BEFORE UPDATE ON sg_colony_npc_emergency_supply "
            "BEGIN SELECT RAISE(ABORT,'immutable emergency stock order'); END");
        sqlite3_close(raw);
        bool rejected=false;
        try{PersistentWorld corrupted(tampered,Simulation(8055));}
        catch(const std::runtime_error&){rejected=true;}
        CHECK(rejected);
    }
    clean(db);clean(recovered);clean(tampered);
    std::cout<<"PASS 04D5C1: actual NPC-origin local habitat food delivery, "
        "crash/restart, durable receipt replay, 200-tick history and corruption refusal\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
