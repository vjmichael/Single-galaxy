#include "galaxy/Persistence.hpp"
#include "galaxy/NpcAppearanceGenome.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
#define CHECK(c) do{if(!(c))throw std::runtime_error(std::string("AUTO04D4D2: ")+ #c+" at "+std::to_string(__LINE__));}while(false)
static void sql(sqlite3* db,const std::string& s){
    char* e=nullptr;
    if(sqlite3_exec(db,s.c_str(),nullptr,nullptr,&e)!=SQLITE_OK){
        const std::string why=e?e:"unknown";sqlite3_free(e);
        throw std::runtime_error("fixture SQL: "+why);
    }
}
int main(){
try{
    const auto path=(std::filesystem::temp_directory_path()/
        ("sg_dependent_v59_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".sqlite")).string();
    const std::uint64_t child=(1ULL<<62)+741;
    EntityId home=0,receiver=0;std::int64_t mass=0;
    constexpr std::uint64_t seed=7419;const std::string scope="v59-child";
    {
        Simulation init(7401);
        home=init.add_colony_ship(7,{0,0},100,2);
        receiver=init.add_colony_ship(7,{0,0},100,2);
        init.provision_colony_genesis(home,100,100,100,200,500);
        init.provision_colony_genesis(receiver,100,100,100,200,500);
        init.enable_individual_workforce();
        init.upgrade_assignment_snapshot_format();
        init.enable_passenger_life_support();
        mass=init.total_material_kg();
        PersistentWorld world(path,std::move(init));
        {
            NpcAppearanceStore family(path,scope,seed,
                [&](std::uint64_t id){return world.world().npc_records().count(id)!=0;});
            family.register_settlement(home,2);
            family.register_settlement(receiver,2);
            for(std::uint64_t id=1;id<=2;++id)family.enroll_family_founder(id,home);
            for(std::uint64_t id=3;id<=4;++id)family.enroll_family_founder(id,receiver);
            family.record_birth(7401,child,1,2,home,1,0,
                [](auto,auto,auto,auto){return true;});
        }
        sqlite3* db=nullptr;CHECK(sqlite3_open(path.c_str(),&db)==SQLITE_OK);
        sql(db,"CREATE TABLE sg_med_society_birth ("
            "event_id TEXT PRIMARY KEY,world_generation INTEGER NOT NULL,"
            "tick INTEGER NOT NULL,family_scope TEXT NOT NULL,"
            "region_scope TEXT NOT NULL,region_revision INTEGER NOT NULL,"
            "owner_generation INTEGER NOT NULL,home_ship TEXT NOT NULL,"
            "regional_colony TEXT NOT NULL,child_id TEXT NOT NULL UNIQUE,"
            "parent_a TEXT NOT NULL,parent_b TEXT NOT NULL,"
            "family_revision INTEGER NOT NULL)");
        sql(db,"INSERT INTO sg_med_society_birth VALUES('7401',1,1,'"+scope+
            "','reg-74',1,1,'"+std::to_string(home)+
            "','colony','"+std::to_string(child)+"','1','2',1)");
        sqlite3_close(db);
        Command cmd{};
        cmd.id=8101;cmd.sequence=8101;cmd.issuer=Simulation::kServerAuthority;
        cmd.actor=home;cmd.target=home;
        cmd.action=Action::HostRegisterDependent;cmd.transport_id=child;
        cmd.passenger_ids={1};cmd.execute_tick=world.world().tick()+1;
        CHECK(world.world().physical_dependents().empty());
        CHECK(world.submit_host_dependent_registration(cmd,scope,seed));
        CHECK(!world.submit_host_dependent_registration(cmd,scope,seed));
        bool blocked=false;
        try{world.step();}catch(const std::runtime_error&){blocked=true;}
        CHECK(blocked);
        bool crashed=false;
        try{world.step_with_dependent_registration(FailPoint::AfterSnapshotWrite);}
        catch(const std::runtime_error&){crashed=true;}
        CHECK(crashed);
        CHECK(world.world().physical_dependents().empty());
        CHECK(world.world().tick()==0);
        world.step_with_dependent_registration();
        CHECK(world.world().physical_dependents().count(child)==1);
        CHECK(world.world().physical_dependents().at(child).guardian_id==1);
        CHECK(world.world().physical_dependents().at(child).home_ship==home);
        CHECK(world.world().npc_records().size()==4); // child is NOT crew
        CHECK(world.world().children_physically_aboard(home)==1);
        CHECK(world.world().total_material_kg()==mass);
        const auto bytes=world.world().canonical_state();
        CHECK(bytes.rfind("galaxy-kernel-v59|",0)==0);
        CHECK(Simulation::from_snapshot(bytes).canonical_state()==bytes);
        world.steps(20-world.world().tick());
        CHECK(world.world().life_support(home)->food_kg==97);
        CHECK(world.world().life_support(home)->water_kg==97);
        CHECK(world.world().life_support(home)->oxygen_kg==97);
        CHECK(world.world().life_support(receiver)->food_kg==98);
        CHECK(world.world().total_material_kg()==mass);
    }
    {
        PersistentWorld recovered(path,Simulation(2));
        CHECK(recovered.world().children_physically_aboard(home)==1);
        CHECK(recovered.world().physical_dependents().at(child).guardian_id==1);
        CHECK(recovered.world().life_support(home)->food_kg==97);
        CHECK(recovered.world().total_material_kg()==mass);
    }
    const auto copy=path+".recovery";
    PersistentWorld::recover_copy(path,copy);
    {
        PersistentWorld replay(copy,Simulation(3));
        CHECK(replay.world().children_physically_aboard(home)==1);
        CHECK(replay.world().physical_dependents().at(child).registration_command==8101);
        CHECK(replay.world().life_support(home)->food_kg==97);
        CHECK(replay.world().total_material_kg()==mass);
    }
    for(const auto& f:{path,copy}){
        std::filesystem::remove(f);std::filesystem::remove(f+"-wal");
        std::filesystem::remove(f+"-shm");
    }
    std::cout<<"PASS v59: real ART02 dependent custody, triple local needs, crash rollback and replay\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
