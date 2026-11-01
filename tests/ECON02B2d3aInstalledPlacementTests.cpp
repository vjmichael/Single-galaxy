#include "galaxy/Persistence.hpp"
#include "galaxy/FactoryLayout.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("B2d3a line ")+std::to_string(__LINE__)+": " #x);}while(0)
static Event issue(PersistentWorld& w,EntityId actor,std::uint64_t id,Action action,
                   EntityId site=0,std::int64_t qty=1,int x=0,int y=0,PlayerId issuer=7){
    Command c{};c.id=id;c.sequence=id;c.issuer=issuer;c.actor=actor;c.target=site;
    c.action=action;c.amount=qty;c.thrust_x=x;c.thrust_y=y;
    c.execute_tick=w.world().tick()+1;
    CHECK(w.submit(c));w.step();return w.world().events().back();
}
static void success(PersistentWorld& w,EntityId ship,std::uint64_t id,Action act,
                    EntityId site=0,std::int64_t qty=1,int x=0,int y=0){
    auto e=issue(w,ship,id,act,site,qty,x,y);
    if(e.result!=Outcome::Applied)std::cerr<<"command "<<id<<" action "<<static_cast<int>(act)<<" result "<<static_cast<int>(e.result)<<"\n";
    CHECK(e.result==Outcome::Applied);
}
int main(){
    auto dir=fs::temp_directory_path()/("econ02b2d3a_"+
              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);auto db=(dir/"state.sqlite").string();
    try{
        Simulation genesis(29003);
        auto ship=genesis.add_colony_ship(7,{0,0},400,8);
        auto other=genesis.add_colony_ship(8,{0,0},200,4);
        genesis.provision_colony_genesis(ship,1000,3000,1000,1000,1000);
        genesis.enable_individual_workforce();
        auto rock=genesis.add_asteroid({1000,0},100);
        const auto mass=genesis.total_material_kg();
        EntityId site=0;std::string checkpoint;
        {
            PersistentWorld w(db,std::move(genesis));
            success(w,ship,1,Action::Mine,rock,60);
            success(w,ship,2,Action::RefineOre,0,60);
            success(w,ship,3,Action::FabricateModule,0,30);
            success(w,ship,4,Action::ConstructOutpost);
            site=w.world().built_outposts().begin()->first;
            // An unbuilt container / workshop can never be positioned.
            CHECK(issue(w,ship,5,Action::PlaceFactoryEquipment,site,0,0,0).result==Outcome::InvalidTarget);
            CHECK(issue(w,ship,6,Action::PlaceFactoryEquipment,site,-1,5,0).result==Outcome::InvalidTarget);
            success(w,ship,7,Action::ConstructStorage,site);
            success(w,ship,8,Action::ConstructStorageNode,site,1);
            success(w,ship,9,Action::ConstructStorageNode,site,2);
            success(w,ship,10,Action::ConstructWorkshop,site);
            const auto modules=w.world().colony_ship(ship)->modules;
            CHECK(issue(w,other,11,Action::PlaceFactoryEquipment,site,0,0,0,8).result!=Outcome::Applied);
            success(w,ship,12,Action::PlaceFactoryEquipment,site,0,0,0);
            CHECK(issue(w,ship,13,Action::PlaceFactoryEquipment,site,1,0,0).result==Outcome::InvalidArgument);
            success(w,ship,14,Action::PlaceFactoryEquipment,site,1,1,0);
            CHECK(issue(w,ship,15,Action::PlaceFactoryEquipment,site,-1,1,0).result==Outcome::InvalidArgument);
            success(w,ship,16,Action::PlaceFactoryEquipment,site,-1,4,0);
            success(w,ship,17,Action::PlaceFactoryEquipment,site,2,-3,-2);
            CHECK(issue(w,ship,18,Action::PlaceFactoryEquipment,site,1,6,0).result==Outcome::InvalidTarget);
            CHECK(w.world().colony_ship(ship)->modules==modules); // no phantom construction
            CHECK(w.world().factory_equipment_position(site,1)->x==1);
            CHECK(w.world().factory_equipment_position(site,kWorkshopConveyorNode)->x==4);
            CHECK(!w.world().factory_equipment_position(site,99));
            CHECK(w.world().total_material_kg()==mass);
            checkpoint=w.world().canonical_state();
            CHECK(checkpoint.rfind("galaxy-kernel-v29|",0)==0);
            CHECK(Simulation::from_snapshot(checkpoint).canonical_state()==checkpoint);
            // Removing one device record must not cause the others to move or create inventory.
            auto independent=Simulation::from_snapshot(checkpoint);
            w.steps(3);independent.steps(3);
            CHECK(independent.canonical_state()==w.world().canonical_state());
            checkpoint=w.world().canonical_state();
        }
        {
            PersistentWorld restored(db,Simulation(43));
            CHECK(restored.world().canonical_state()==checkpoint);
            CHECK(restored.world().factory_equipment_position(site,2)->x==-3);
            CHECK(restored.world().total_material_kg()==mass);
            const auto state=restored.world().canonical_state();
            bool failed=false;try{restored.step(FailPoint::AfterSnapshotWrite);}catch(...){failed=true;}
            CHECK(failed);CHECK(restored.world().canonical_state()==state);
        }
        {
            PersistentWorld restored(db,Simulation(44));
            CHECK(restored.world().canonical_state()==checkpoint);
        }
        // A malformed spatial snapshot must fail independently of SQLite digest checks.
        auto forged=checkpoint;
        auto pos=forged.find("factory_placement|"+std::to_string(site)+"|1|1|0\n");
        CHECK(pos!=std::string::npos);
        forged.replace(pos,std::string("factory_placement|"+std::to_string(site)+"|1|1|0\n").size(),
                       "factory_placement|"+std::to_string(site)+"|1|0|0\n");
        bool detected=false;try{(void)Simulation::from_snapshot(forged);}catch(...){detected=true;}
        CHECK(detected);
        fs::remove_all(dir);
        std::cout<<"ECON-02B2d3a durable installed placements, collision, recovery PASS\n";
        return 0;
    }catch(const std::exception& ex){
        std::cerr<<"ECON-02B2d3a FAILED: "<<ex.what()<<'\n';
        fs::remove_all(dir);return 1;
    }
}
