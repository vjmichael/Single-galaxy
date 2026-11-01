#include "galaxy/Persistence.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do {if(!(x))throw std::runtime_error("B1c line "+std::to_string(__LINE__)+": " #x);}while(0)
static Event send(PersistentWorld& w,EntityId ship,std::uint64_t id,Action a,EntityId site=0,std::int64_t amount=1,int item=0,PlayerId issuer=7){
    Command c{};c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;c.issuer=issuer;
    c.actor=ship;c.target=site;c.action=a;c.amount=amount;c.thrust_x=item;
    CHECK(w.submit(c));w.step();return w.world().events().back();
}
static void ok(PersistentWorld& w,EntityId ship,std::uint64_t id,Action a,EntityId site=0,std::int64_t amount=1,int item=0){
    CHECK(send(w,ship,id,a,site,amount,item).result==Outcome::Applied);
}
int main(){
    const auto dir=fs::temp_directory_path()/("econ02b1c_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    const auto db=(dir/"world.sqlite").string();
    try {
        Simulation s(90210);const auto ship=s.add_colony_ship(7,{0,0},200,8);
        s.provision_colony_genesis(ship,1000,3000,1000,1000,1000);
        const auto ore=s.add_asteroid({2000,0},150);
        const auto silicate=s.add_geological_asteroid({1000,0},AsteroidClass::Silicate,30,2500);
        const auto extra_ore=s.add_asteroid({1000,0},400);
        s.enable_individual_workforce();
        const auto conserved=s.total_material_kg();EntityId site=0;
        {
            PersistentWorld w(db,std::move(s));
            ok(w,ship,1,Action::Mine,ore,80);
            ok(w,ship,2,Action::RefineOre,0,80);
            ok(w,ship,3,Action::FabricateModule,0,40);
            ok(w,ship,4,Action::ConstructOutpost);
            site=w.world().built_outposts().begin()->first;
            ok(w,ship,5,Action::ConstructWorkshop,site);
            ok(w,ship,6,Action::ConstructWorkshopFinisher,site);
            ok(w,ship,7,Action::ConstructUtility,site);
            ok(w,ship,8,Action::ConstructHabitat,site);
            for(int i=1;i<=3;++i)ok(w,ship,8+i,Action::TransferHabitatStock,site,500,i);
            ok(w,ship,12,Action::AssignHabitatResidents,site,2);
            ok(w,ship,13,Action::AssignUtilityTechnicians,site,1);
            ok(w,ship,14,Action::TransferUtilitySpares,site,25);
            ok(w,ship,15,Action::SetUtilityMaintenance,site,1);
            std::uint64_t first=0,second=0;
            for(const auto& [id,n]:w.world().npc_records())if(n.location==site && n.role==NpcRole::Habitat){if(!first)first=id;else if(!second)second=id;}
            CHECK(first && second);
            ok(w,ship,18,Action::Mine,silicate,45);
            ok(w,ship,19,Action::RefineMineral,10,18000);
            const auto shipBefore=w.world().ship_cargo_volume_liters(ship);
            CHECK(shipBefore%1000==0);
            CHECK(w.world().colony_ship(ship)->goods_kg.at("aluminum")==3600);
            // New transfers do NOT require a 1000kg feed or mixed 1000kg pallet.
            ok(w,ship,20,Action::TransferWorkshopStack,site,37,0);
            CHECK(w.world().ship_cargo_volume_liters(ship)==shipBefore-37);
            CHECK(w.world().workshop(site)->aluminum_kg==37);
            CHECK(w.world().workshop_bin_inventory_snapshot(site,0).amount("good/aluminum")==37);
            CHECK(w.world().ship_inventory_snapshot(ship).amount("good/aluminum")==3563);
            CHECK(send(w,ship,21,Action::TransferWorkshopStack,site,-50,0).result==Outcome::NoResources);
            CHECK(w.world().workshop(site)->aluminum_kg==37);
            ok(w,ship,22,Action::TransferWorkshopStack,site,-37,0);
            CHECK(w.world().ship_cargo_volume_liters(ship)==shipBefore);
            ok(w,ship,36,Action::AssignWorkshopOperator,site,first);
            ok(w,ship,37,Action::AssignWorkshopFinisher,site,second);
            ok(w,ship,23,Action::TransferWorkshopFeed,site,1000);
            w.steps(4);
            CHECK(w.world().workshop(site)->panels_kg==19);
            CHECK(w.world().workshop(site)->scrap_kg==1);
            auto starting=w.world().ship_cargo_volume_liters(ship);
            ok(w,ship,24,Action::TransferWorkshopStack,site,-1,1); // just one panel
            CHECK(w.world().ship_cargo_volume_liters(ship)==starting+1);
            CHECK(w.world().colony_ship(ship)->goods_kg.at("aluminum_panels")==1);
            CHECK(w.world().ship_inventory_snapshot(ship).amount("good/aluminum_panels")==1);
            ok(w,ship,25,Action::TransferWorkshopStack,site,-1,3); // scrap separately
            CHECK(w.world().colony_ship(ship)->goods_kg.at("aluminum_scrap")==1);
            CHECK(w.world().total_material_kg()==conserved);
            auto saved=w.world().canonical_state();
            CHECK(saved.rfind("galaxy-kernel-v24|",0)==0);
            CHECK(Simulation::from_snapshot(saved).canonical_state()==saved);
            // Return individual stacks and prove arbitrary kilogram slices survive.
            ok(w,ship,26,Action::TransferWorkshopStack,site,1,1);
            ok(w,ship,27,Action::TransferWorkshopStack,site,1,3);
            CHECK(w.world().ship_cargo_volume_liters(ship)==starting);
            w.steps(19);
            CHECK(w.world().workshop(site)->frames_kg>=19);
            const auto before=w.world().ship_cargo_volume_liters(ship);
            ok(w,ship,28,Action::TransferWorkshopStack,site,-7,2);
            CHECK(w.world().colony_ship(ship)->goods_kg.at("aluminum_frames")==7);
            CHECK(w.world().ship_cargo_volume_liters(ship)==before+7);
            ok(w,ship,29,Action::TransferWorkshopStack,site,7,2);
            CHECK(w.world().ship_cargo_volume_liters(ship)==before);
            // Fill the ship by normal mining: m3 capacity alone decides whether
            // even a one-unit new item may be loaded (no mass / density rule).
            CHECK(w.world().ship_cargo_volume_liters(ship)%1000==0);
            ok(w,ship,30,Action::Mine,extra_ore,400);
            CHECK(w.world().ship_cargo_volume_liters(ship)==w.world().find(ship)->cargo_capacity*1000);
            auto kept=w.world().workshop(site)->frames_kg;
            CHECK(send(w,ship,31,Action::TransferWorkshopStack,site,-1,2).result==Outcome::CargoFull);
            CHECK(w.world().workshop(site)->frames_kg==kept);
            // One litre of available volume is enough to move precisely one
            // item unit: empty 1L by delivering aluminum feed.
            ok(w,ship,32,Action::TransferWorkshopStack,site,1,0);
            CHECK(w.world().ship_cargo_volume_liters(ship)==w.world().find(ship)->cargo_capacity*1000-1);
            ok(w,ship,33,Action::TransferWorkshopStack,site,-1,2);
            CHECK(w.world().ship_cargo_volume_liters(ship)==w.world().find(ship)->cargo_capacity*1000);
            CHECK(w.world().total_material_kg()==conserved);
            // Strict selector and user authorization; no forged item names.
            Command bad{};bad.id=34;bad.sequence=34;bad.execute_tick=w.world().tick()+1;bad.issuer=7;bad.actor=ship;bad.target=site;bad.action=Action::TransferWorkshopStack;bad.thrust_x=9;bad.amount=-1;
            CHECK(!w.submit(bad));
            CHECK(send(w,ship,35,Action::TransferWorkshopStack,site,-1,1,9).result==Outcome::Unauthorized);
            const auto state=w.world().canonical_state();
            CHECK(Simulation::from_snapshot(state).canonical_state()==state);
            PersistentWorld::recover_copy(db,(dir/"copy.sqlite").string());
        }
        {
            PersistentWorld restored(db,Simulation(1));
            CHECK(restored.world().total_material_kg()==conserved);
            CHECK(restored.world().find(ship)->cargo_capacity*1000==restored.world().ship_cargo_volume_liters(ship));
        }
        {
            PersistentWorld recovered((dir/"copy.sqlite").string(),Simulation(1));
            CHECK(recovered.world().total_material_kg()==conserved);
        }
        const auto forged=(dir/"forged.sqlite").string();fs::copy_file(db,forged);
        sqlite3* conn=nullptr;CHECK(sqlite3_open(forged.c_str(),&conn)==SQLITE_OK);
        char* err=nullptr;CHECK(sqlite3_exec(conn,"UPDATE resource_ledger SET destination_id='fake:inventory' WHERE reason='workshop_stack_transfer_kg' AND amount=7 AND destination_id LIKE 'goods:%'",nullptr,nullptr,&err)==SQLITE_OK);
        if(err)sqlite3_free(err);CHECK(sqlite3_changes(conn)==1);sqlite3_close(conn);
        bool rejected=false;try{PersistentWorld invalid(forged,Simulation(1));}catch(const std::runtime_error&){rejected=true;}
        CHECK(rejected);
        fs::remove_all(dir);
        std::cout<<"ECON-02B1c exact stacks, m3 cargo, backpressure, persistence, tamper: PASS\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"ECON-02B1c FAIL: "<<e.what()<<'\n';fs::remove_all(dir);return 1;}
}
