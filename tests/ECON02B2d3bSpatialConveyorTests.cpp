#include "galaxy/Persistence.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("B2d3b line ")+std::to_string(__LINE__)+": " #x);}while(0)
static Event issue(PersistentWorld& w,EntityId ship,std::uint64_t id,Action action,
                   EntityId target=0,std::int64_t amount=1,int x=0,int y=0,PlayerId owner=7){
    Command c{};c.id=id;c.sequence=id;c.issuer=owner;c.actor=ship;c.target=target;
    c.action=action;c.amount=amount;c.thrust_x=x;c.thrust_y=y;
    c.execute_tick=w.world().tick()+1;
    CHECK(w.submit(c));w.step();return w.world().events().back();
}
static void ok(PersistentWorld& w,EntityId ship,std::uint64_t id,Action act,
               EntityId target=0,std::int64_t amount=1,int x=0,int y=0){
    const auto ev=issue(w,ship,id,act,target,amount,x,y);
    if(ev.result!=Outcome::Applied)std::cerr<<"action "<<static_cast<int>(act)<<" failed outcome "<<static_cast<int>(ev.result)<<'\n';
    CHECK(ev.result==Outcome::Applied);
}
static std::int64_t stock(const PersistentWorld& w,EntityId site,std::uint32_t node){
    return w.world().storage_node_inventory_snapshot(site,node).amount("good/aluminum");
}
int main(){
    const auto dir=fs::temp_directory_path()/("econ02b2d3b_"+
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);const auto db=(dir/"test.sqlite").string();
    try{
        Simulation start(30037);
        const auto ship=start.add_colony_ship(7,{0,0},400,8);
        const auto other=start.add_colony_ship(8,{0,0},50,4);
        start.provision_colony_genesis(ship,1000,3000,1000,1000,1000);
        const auto rock=start.add_asteroid({1000,0},100);
        const auto ore=start.add_geological_asteroid({1000,0},AsteroidClass::Silicate,30,2500);
        start.enable_individual_workforce();
        const auto original_mass=start.total_material_kg();
        EntityId site=0;
        std::string final_state,unfinished;
        {
            PersistentWorld w(db,std::move(start));
            ok(w,ship,1,Action::Mine,rock,80);
            ok(w,ship,2,Action::RefineOre,0,80);
            ok(w,ship,3,Action::FabricateModule,0,40);
            ok(w,ship,4,Action::ConstructOutpost);
            site=w.world().built_outposts().begin()->first;
            ok(w,ship,5,Action::ConstructStorage,site);
            ok(w,ship,6,Action::ConstructStorageNode,site,1);
            ok(w,ship,7,Action::ConstructStorageNode,site,2);
            ok(w,ship,8,Action::PlaceFactoryEquipment,site,1,0,0);
            ok(w,ship,9,Action::PlaceFactoryEquipment,site,2,4,0);
            ok(w,ship,10,Action::Mine,ore,45);
            ok(w,ship,11,Action::RefineMineral,10,18000);
            ok(w,ship,12,Action::TransferStorageNodeStack,site,350,4,0);
            CHECK(stock(w,site,1)==350 && stock(w,site,2)==0);
            const auto material_before=w.world().total_material_kg();
            CHECK(material_before==original_mass);
            const auto modules=w.world().colony_ship(ship)->modules;
            ok(w,ship,13,Action::ConstructStorageLink,site,28,1,2);
            CHECK(w.world().colony_ship(ship)->modules==modules-1);
            CHECK(w.world().storage_link(site,13)->route_required);
            CHECK(!w.world().storage_link(site,13)->route_ready);
            CHECK(w.world().storage_link(site,13)->units_moved==0);
            CHECK(stock(w,site,1)==350 && stock(w,site,2)==0);
            // Site and identity authorization is enforced for every installed segment.
            CHECK(issue(w,other,14,Action::PlaceConveyorCell,site,13,1,0,8).result!=Outcome::Applied);
            CHECK(issue(w,ship,15,Action::PlaceConveyorCell,site,13,4,0).result==Outcome::InvalidArgument);
            CHECK(issue(w,ship,16,Action::PlaceConveyorCell,site,13,2,0).result==Outcome::InvalidArgument);
            CHECK(w.world().colony_ship(ship)->modules==modules-1);
            ok(w,ship,17,Action::PlaceConveyorCell,site,13,1,0);
            CHECK(w.world().total_material_kg()==original_mass);
            CHECK(issue(w,ship,18,Action::PlaceConveyorCell,site,13,1,0).result==Outcome::InvalidArgument);
            CHECK(issue(w,ship,19,Action::PlaceConveyorCell,site,13,3,0).result==Outcome::InvalidArgument);
            ok(w,ship,20,Action::PlaceConveyorCell,site,13,1,1);
            CHECK(issue(w,ship,21,Action::CompleteConveyorRoute,site,13).result==Outcome::InvalidArgument);
            CHECK(stock(w,site,1)==350 && stock(w,site,2)==0);
            unfinished=w.world().canonical_state();
            CHECK(unfinished.rfind("galaxy-kernel-v31|",0)==0);
            CHECK(Simulation::from_snapshot(unfinished).canonical_state()==unfinished);
            CHECK(w.world().storage_link(site,13)->route_cells.size()==2);
            CHECK(w.world().colony_ship(ship)->modules==modules-3);
            CHECK(w.world().total_material_kg()==original_mass);
        }
        {
            PersistentWorld w(db,Simulation(90));
            CHECK(w.world().canonical_state()==unfinished);
            CHECK(w.world().storage_link(site,13)->route_required && !w.world().storage_link(site,13)->route_ready);
            w.steps(4);CHECK(stock(w,site,1)==350 && stock(w,site,2)==0);
            ok(w,ship,22,Action::PlaceConveyorCell,site,13,2,1);
            ok(w,ship,23,Action::PlaceConveyorCell,site,13,3,1);
            ok(w,ship,27,Action::PlaceConveyorCell,site,13,3,0); // a genuine 90-degree corner
            CHECK(w.world().total_material_kg()==original_mass);
            ok(w,ship,28,Action::CompleteConveyorRoute,site,13);
            CHECK(w.world().storage_link(site,13)->route_ready);
            CHECK(w.world().storage_link(site,13)->units_moved==7);
            CHECK(stock(w,site,1)==343 && stock(w,site,2)==7);
            CHECK(issue(w,ship,24,Action::PlaceConveyorCell,site,13,3,1).result==Outcome::InvalidTarget);
            CHECK(w.world().total_material_kg()==original_mass);
            const auto before=w.world().canonical_state();
            auto remote=Simulation::from_snapshot(before);
            w.steps(15);remote.steps(15);
            CHECK(remote.canonical_state()==w.world().canonical_state());
            CHECK(w.world().storage_link(site,13)->units_moved==119);
            CHECK(stock(w,site,2)==119);
            CHECK(w.world().total_material_kg()==original_mass);
            final_state=w.world().canonical_state();
            bool fault=false;
            try{w.step(FailPoint::AfterSnapshotWrite);}catch(...){fault=true;}
            CHECK(fault);CHECK(w.world().canonical_state()==final_state);
        }
        {
            PersistentWorld w(db,Simulation(8));
            CHECK(w.world().canonical_state()==final_state);
            CHECK(w.world().total_material_kg()==original_mass);
        }
        // Intrinsic layout validation must reject a path segment moved inside equipment.
        auto forged=final_state;
        const auto prefix="factory_conveyor|"+std::to_string(site)+"|13|1|1:0;1:1;2:1;3:1;3:0\n";
        const auto at=forged.find(prefix);CHECK(at!=std::string::npos);
        forged.replace(at,prefix.size(),"factory_conveyor|"+std::to_string(site)+"|13|1|1:0;1:1;4:0;3:1;3:0\n");
        bool detected=false;try{(void)Simulation::from_snapshot(forged);}catch(...){detected=true;}
        CHECK(detected);
        sqlite3* conn=nullptr;CHECK(sqlite3_open(db.c_str(),&conn)==SQLITE_OK);
        CHECK(sqlite3_exec(conn,"UPDATE resource_ledger SET destination_id='forged' WHERE id=(SELECT MIN(id) FROM resource_ledger WHERE reason='conveyor_cell_construction_t')",nullptr,nullptr,nullptr)==SQLITE_OK);
        sqlite3_close(conn);
        detected=false;try{PersistentWorld invalid(db,Simulation(82));(void)invalid;}catch(...){detected=true;}
        CHECK(detected);
        fs::remove_all(dir);
        std::cout<<"ECON-02B2d3b spatial route construction, durable activation, ledger PASS\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"ECON-02B2d3b FAIL: "<<e.what()<<'\n';fs::remove_all(dir);return 1;
    }
}
