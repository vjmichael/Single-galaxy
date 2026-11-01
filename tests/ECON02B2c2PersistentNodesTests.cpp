#include "galaxy/Persistence.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("B2c2 line ")+std::to_string(__LINE__)+": "#x);}while(0)
static Event issue(PersistentWorld& w,EntityId ship,std::uint64_t id,Action a,EntityId target=0,
                   std::int64_t amount=1,int x=0,int y=0,PlayerId issuer=7){
  Command c{};c.id=id;c.sequence=id;c.issuer=issuer;c.actor=ship;c.action=a;c.target=target;
  c.amount=amount;c.thrust_x=x;c.thrust_y=y;c.execute_tick=w.world().tick()+1;
  CHECK(w.submit(c));w.step();return w.world().events().back();
}
static void ok(PersistentWorld& w,EntityId ship,std::uint64_t id,Action a,EntityId target=0,
               std::int64_t amount=1,int x=0,int y=0){CHECK(issue(w,ship,id,a,target,amount,x,y).result==Outcome::Applied);}
int main(){
 const auto dir=fs::temp_directory_path()/("econ02b2c2_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 fs::create_directories(dir);const auto db=(dir/"world.sqlite").string();
 try{
    Simulation start(98026);const auto ship=start.add_colony_ship(7,{0,0},300,8);
    start.provision_colony_genesis(ship,1000,3000,1000,1000,1000);
    const auto rock=start.add_asteroid({1000,0},100);
    const auto silicate=start.add_geological_asteroid({1000,0},AsteroidClass::Silicate,30,2500);
    const auto intruder=start.add_colony_ship(8,{0,0},50,4);
    start.enable_individual_workforce();const auto initial=start.total_material_kg();
    EntityId site=0;
    {
        PersistentWorld w(db,std::move(start));
        ok(w,ship,1,Action::Mine,rock,80);ok(w,ship,2,Action::RefineOre,0,80);
        ok(w,ship,3,Action::FabricateModule,0,40);ok(w,ship,4,Action::ConstructOutpost);
        site=w.world().built_outposts().begin()->first;
        ok(w,ship,5,Action::ConstructStorage,site);
        ok(w,ship,6,Action::ConstructStorageNode,site,1);
        ok(w,ship,7,Action::ConstructStorageNode,site,2);
        CHECK(w.world().storage(site)->extra_bins.size()==2);
        CHECK(w.world().storage_inventory_snapshot(site).capacity_liters()==4000);
        CHECK(w.world().storage_node_inventory_snapshot(site,1).capacity_liters()==4000);
        CHECK(w.world().storage_node_inventory_snapshot(site,2).capacity_liters()==4000);
        CHECK(issue(w,ship,8,Action::ConstructStorageNode,site,1).result==Outcome::InvalidTarget);
        CHECK(issue(w,intruder,9,Action::TransferStorageNodeStack,site,1,4,0,8).result!=Outcome::Applied);
        CHECK(issue(w,ship,10,Action::TransferStorageNodeStack,site,1,12,0).result==Outcome::InvalidTarget);
        ok(w,ship,11,Action::Mine,silicate,45);
        ok(w,ship,12,Action::RefineMineral,10,18000);
        ok(w,ship,13,Action::Mine,silicate,45);
        ok(w,ship,14,Action::RefineMineral,10,18000);
        CHECK(w.world().ship_inventory_snapshot(ship).amount("good/aluminum")>=4000);
        CHECK(w.world().total_material_kg()==initial);
        // Each node is a separate finite bin and one must not mutate the other.
        ok(w,ship,15,Action::TransferStorageNodeStack,site,1000,4,0);
        ok(w,ship,16,Action::TransferStorageNodeStack,site,500,8,0);
        CHECK(w.world().storage_node_inventory_snapshot(site,1).amount("good/aluminum")==1000);
        CHECK(w.world().storage_node_inventory_snapshot(site,2).amount("good/aluminum")==500);
        CHECK(w.world().storage_inventory_snapshot(site).used_liters()==0);
        // Negative path must not withdraw existing goods.
        CHECK(issue(w,ship,17,Action::TransferStorageNodeStack,site,100,5,1).result==Outcome::NoResources);
        CHECK(w.world().storage_node_inventory_snapshot(site,1).amount("good/aluminum")==1000);
        // Another 1000 to the second bin, then a reverse transfer of 7 units.
        ok(w,ship,18,Action::TransferStorageNodeStack,site,1000,8,0);
        ok(w,ship,19,Action::TransferStorageNodeStack,site,7,8,1);
        CHECK(w.world().storage_node_inventory_snapshot(site,2).amount("good/aluminum")==1493);
        CHECK(w.world().total_material_kg()==initial);
        CHECK(w.world().canonical_state().rfind("galaxy-kernel-v26|",0)==0);
        CHECK(Simulation::from_snapshot(w.world().canonical_state()).canonical_state()==w.world().canonical_state());
        // Extra node state must be journaled and restored with no phantom changes.
    }
    {
        PersistentWorld w(db,Simulation(123));
        CHECK(w.world().storage_node_inventory_snapshot(site,1).amount("good/aluminum")==1000);
        CHECK(w.world().storage_node_inventory_snapshot(site,2).amount("good/aluminum")==1493);
        CHECK(w.world().total_material_kg()==initial);
        ok(w,ship,20,Action::TransferStorageNodeStack,site,500,4,1);
        CHECK(w.world().storage_node_inventory_snapshot(site,1).amount("good/aluminum")==500);
        CHECK(w.world().total_material_kg()==initial);
        // Additional *real* mined aluminum demonstrates that the combined
        // site capacity is not erroneously limited to one container's 4m3.
        ok(w,ship,25,Action::Mine,silicate,45);
        ok(w,ship,26,Action::RefineMineral,10,18000);
        CHECK(w.world().ship_inventory_snapshot(ship).amount("good/aluminum")>=2507);
        CHECK(w.world().total_material_kg()==initial);
        // Full second bin blocks new goods and leaves the source untouched.
        CHECK(issue(w,ship,21,Action::TransferStorageNodeStack,site,1000,8,0).result==Outcome::Applied);
        CHECK(issue(w,ship,22,Action::TransferStorageNodeStack,site,1000,8,0).result==Outcome::Applied);
        CHECK(issue(w,ship,23,Action::TransferStorageNodeStack,site,507,8,0).result==Outcome::Applied);
        CHECK(w.world().storage_node_inventory_snapshot(site,2).used_liters()==4000);
        CHECK(w.world().storage_node_inventory_snapshot(site,1).used_liters()==500);
        CHECK(issue(w,ship,24,Action::TransferStorageNodeStack,site,1,8,0).result==Outcome::CargoFull);
        CHECK(w.world().storage_node_inventory_snapshot(site,2).used_liters()==4000);
        CHECK(w.world().total_material_kg()==initial);
    }
    sqlite3* dbh=nullptr;CHECK(sqlite3_open(db.c_str(),&dbh)==SQLITE_OK);
    CHECK(sqlite3_exec(dbh,"UPDATE resource_ledger SET destination_id='fake' WHERE id=(SELECT MIN(id) FROM resource_ledger WHERE reason='storage_node_stack_transfer_kg')",nullptr,nullptr,nullptr)==SQLITE_OK);
    sqlite3_close(dbh);
    bool caught=false;try{PersistentWorld w(db,Simulation(888));(void)w;}catch(const std::exception&){caught=true;}
    CHECK(caught);
    fs::remove_all(dir);
    std::cout<<"ECON-02B2c2 durable real extra storage nodes and ledger tamper PASS\n";return 0;
 }catch(const std::exception& e){std::cerr<<"ECON-02B2c2 FAIL: "<<e.what()<<'\n';fs::remove_all(dir);return 1;}
}
