#include "galaxy/Persistence.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(expr) do {if(!(expr))throw std::runtime_error(std::string("B2b line ")+std::to_string(__LINE__)+": " #expr);}while(0)
static Event send(PersistentWorld& w,EntityId ship,std::uint64_t id,Action a,EntityId site=0,
                  std::int64_t amount=1,int item=0,int direction=0,PlayerId issuer=7){
    Command c{};c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
    c.issuer=issuer;c.actor=ship;c.target=site;c.action=a;c.amount=amount;
    c.thrust_x=item;c.thrust_y=direction;
    CHECK(w.submit(c));w.step();return w.world().events().back();
}
static void ok(PersistentWorld& w,EntityId ship,std::uint64_t id,Action a,EntityId site=0,
               std::int64_t amount=1,int item=0,int direction=0){
    CHECK(send(w,ship,id,a,site,amount,item,direction).result==Outcome::Applied);
}
int main(){
 const auto dir=fs::temp_directory_path()/("econ02b2b_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 fs::create_directories(dir);const auto db=(dir/"world.sqlite").string();
 try{
    Simulation start(98024);const auto ship=start.add_colony_ship(7,{0,0},300,8);
    start.provision_colony_genesis(ship,1000,3000,1000,1000,1000);
    const auto rock=start.add_asteroid({1000,0},100);
    const auto silicate=start.add_geological_asteroid({1000,0},AsteroidClass::Silicate,30,2500);
    const auto other=start.add_colony_ship(8,{0,0},50,4);
    start.enable_individual_workforce();
    const auto conserved=start.total_material_kg();EntityId site=0;
    {
      PersistentWorld w(db,std::move(start));
      ok(w,ship,1,Action::Mine,rock,80);
      ok(w,ship,2,Action::RefineOre,0,80);
      ok(w,ship,3,Action::FabricateModule,0,40);
      ok(w,ship,4,Action::ConstructOutpost);
      site=w.world().built_outposts().begin()->first;
      CHECK(send(w,ship,5,Action::ConstructStorage,site).result==Outcome::Applied);
      CHECK(w.world().storage(site));
      CHECK(w.world().storage(site)->source_ship==ship);
      CHECK(w.world().storage_inventory_snapshot(site).capacity_liters()==4000);
      CHECK(w.world().storage_inventory_snapshot(site).used_liters()==0);
      CHECK(send(w,ship,6,Action::ConstructStorage,site).result!=Outcome::Applied);
      ok(w,ship,7,Action::ConstructWorkshop,site);
      // Real outpost services and resident workforce support actual manufactured goods.
      ok(w,ship,30,Action::ConstructUtility,site);
      ok(w,ship,31,Action::ConstructHabitat,site);
      for(int i=1;i<=3;++i)ok(w,ship,31+i,Action::TransferHabitatStock,site,500,i);
      ok(w,ship,35,Action::AssignHabitatResidents,site,2);
      ok(w,ship,36,Action::AssignUtilityTechnicians,site,1);
      ok(w,ship,37,Action::TransferUtilitySpares,site,25);
      ok(w,ship,38,Action::SetUtilityMaintenance,site,1);
      std::uint64_t operator_id=0;
      for(const auto& [id,n]:w.world().npc_records())
          if(n.location==site && n.role==NpcRole::Habitat){operator_id=id;break;}
      CHECK(operator_id);
      // New storage commands are still bound to ship ownership and its actual location.
      CHECK(send(w,other,8,Action::TransferStorageStack,site,1,0,0,8).result!=Outcome::Applied);
      CHECK(send(w,ship,9,Action::TransferStorageStack,site,1,0,1).result==Outcome::NoResources);
      ok(w,ship,10,Action::Mine,silicate,45);
      ok(w,ship,11,Action::RefineMineral,10,18000);
      ok(w,ship,12,Action::Mine,silicate,45);
      ok(w,ship,13,Action::RefineMineral,10,18000);
      CHECK(w.world().ship_inventory_snapshot(ship).amount("good/aluminum")>=4000);
      const auto before=w.world().total_material_kg();CHECK(before==conserved);
      // Ship -> physical container; batches may be smaller than a tonne.
      ok(w,ship,14,Action::TransferStorageStack,site,750,0,0);
      ok(w,ship,15,Action::TransferStorageStack,site,1000,0,0);
      CHECK(w.world().storage_inventory_snapshot(site).amount("good/aluminum")==1750);
      // Container -> workshop; the workshop has a separate 1000-unit input bin.
      ok(w,ship,16,Action::TransferStorageStack,site,100,0,3);
      CHECK(w.world().storage_inventory_snapshot(site).amount("good/aluminum")==1650);
      CHECK(w.world().workshop(site)->aluminum_kg==100);
      // Workshop -> container: true material transfer, no phantom additional material.
      ok(w,ship,17,Action::TransferStorageStack,site,25,0,2);
      CHECK(w.world().workshop(site)->aluminum_kg==75);
      CHECK(w.world().storage_inventory_snapshot(site).amount("good/aluminum")==1675);
      // Fill exact 4m3 limit in small stacks.
      ok(w,ship,18,Action::TransferStorageStack,site,1000,0,0);
      ok(w,ship,19,Action::TransferStorageStack,site,1000,0,0);
      ok(w,ship,20,Action::TransferStorageStack,site,325,0,0);
      CHECK(w.world().storage_inventory_snapshot(site).used_liters()==4000);
      const auto keep=w.world().canonical_state();
      CHECK(keep.rfind("galaxy-kernel-v25|",0)==0);
      CHECK(send(w,ship,21,Action::TransferStorageStack,site,1,0,0).result==Outcome::CargoFull);
      CHECK(w.world().storage_inventory_snapshot(site).used_liters()==4000);
      CHECK(send(w,ship,22,Action::TransferStorageStack,site,1,1,1).result==Outcome::NoResources);
      CHECK(w.world().canonical_state()!=keep); // failed commands are journaled
      CHECK(Simulation::from_snapshot(w.world().canonical_state()).canonical_state()==w.world().canonical_state());
      ok(w,ship,23,Action::TransferStorageStack,site,17,0,1);
      CHECK(w.world().storage_inventory_snapshot(site).free_liters()==17);
      CHECK(w.world().total_material_kg()==conserved);
      // Restore from SQLite rechecks resource ledger and authoritative state.
    }
    {
      PersistentWorld w(db,Simulation(111));
      CHECK(w.world().storage(site));
      CHECK(w.world().storage_inventory_snapshot(site).amount("good/aluminum")==3983);
      CHECK(w.world().total_material_kg()==conserved);
      ok(w,ship,24,Action::TransferStorageStack,site,17,0,0);
      CHECK(w.world().storage_inventory_snapshot(site).free_liters()==0);
      CHECK(w.world().total_material_kg()==conserved);
      // Workshop production still obeys full-batch inputs and powered labor.
      std::uint64_t resumed_operator=0;
      for(const auto& [id,n]:w.world().npc_records())
          if(n.location==site && n.role==NpcRole::Habitat){resumed_operator=id;break;}
      CHECK(resumed_operator);
      ok(w,ship,39,Action::AssignWorkshopOperator,site,resumed_operator);
      w.steps(6);
      CHECK(w.world().workshop(site)->panels_kg>=19);
      CHECK(w.world().workshop(site)->scrap_kg>=1);
      CHECK(send(w,ship,40,Action::TransferStorageStack,site,1,1,2).result==Outcome::CargoFull);
      const auto panels_before=w.world().workshop(site)->panels_kg;
      CHECK(w.world().workshop(site)->panels_kg==panels_before);
      ok(w,ship,41,Action::TransferStorageStack,site,40,0,1);
      ok(w,ship,42,Action::TransferStorageStack,site,19,1,2);
      ok(w,ship,43,Action::TransferStorageStack,site,1,3,2);
      auto mixed=w.world().storage_inventory_snapshot(site);
      CHECK(mixed.amount("good/aluminum")==3960);
      CHECK(mixed.amount("good/aluminum_panels")==19);
      CHECK(mixed.amount("good/aluminum_scrap")==1);
      CHECK(mixed.used_liters()==3980);
      CHECK(w.world().total_material_kg()==conserved);
    }
    // Tampering a receipt endpoint cannot be treated as a legitimate shipment.
    sqlite3* raw=nullptr;CHECK(sqlite3_open(db.c_str(),&raw)==SQLITE_OK);
    CHECK(sqlite3_exec(raw,"UPDATE resource_ledger SET destination_id='forged' WHERE id=(SELECT MIN(id) FROM resource_ledger WHERE reason='storage_stack_transfer_kg')",nullptr,nullptr,nullptr)==SQLITE_OK);
    sqlite3_close(raw);
    bool refused=false;
    try{PersistentWorld tampered(db,Simulation(222));(void)tampered;}catch(const std::exception&){refused=true;}
    CHECK(refused);
    fs::remove_all(dir);
    std::cout<<"ECON-02B2b constructed mixed-volume container, transfer, conservation, recovery, tamper PASS\n";
    return 0;
 }catch(const std::exception& e){std::cerr<<"ECON-02B2b FAIL: "<<e.what()<<'\n';fs::remove_all(dir);return 1;}
}
