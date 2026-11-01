#include "galaxy/Persistence.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do {if(!(x))throw std::runtime_error(std::string("B2d2 line ")+std::to_string(__LINE__)+": " #x);}while(0)
static Event issue(PersistentWorld& w,EntityId ship,std::uint64_t id,Action action,
                   EntityId target=0,std::int64_t amount=1,std::int64_t x=0,
                   std::int64_t y=0,PlayerId issuer=7){
    Command c{};c.id=id;c.sequence=id;c.issuer=issuer;c.actor=ship;c.target=target;
    c.action=action;c.amount=amount;c.thrust_x=x;c.thrust_y=y;
    c.execute_tick=w.world().tick()+1;
    CHECK(w.submit(c));w.step();return w.world().events().back();
}
static void ok(PersistentWorld& w,EntityId ship,std::uint64_t id,Action a,
               EntityId site=0,std::int64_t amount=1,std::int64_t x=0,std::int64_t y=0){
    CHECK(issue(w,ship,id,a,site,amount,x,y).result==Outcome::Applied);
}
int main(){
    auto dir=fs::temp_directory_path()/("econ02b2d2_"+
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);const auto db=(dir/"world.sqlite").string();
    try{
        Simulation genesis(28002);
        const auto ship=genesis.add_colony_ship(7,{0,0},400,8);
        const auto stranger=genesis.add_colony_ship(8,{0,0},200,4);
        genesis.provision_colony_genesis(ship,1000,3000,1000,1000,1000);
        const auto ore=genesis.add_asteroid({1000,0},150);
        const auto silicate=genesis.add_geological_asteroid({1000,0},AsteroidClass::Silicate,30,2500);
        genesis.enable_individual_workforce();
        const auto conservation=genesis.total_material_kg();
        EntityId site=0;std::string saved;
        {
            PersistentWorld w(db,std::move(genesis));
            ok(w,ship,1,Action::Mine,ore,80);
            ok(w,ship,2,Action::RefineOre,0,80);
            ok(w,ship,3,Action::FabricateModule,0,40);
            ok(w,ship,4,Action::ConstructOutpost);
            site=w.world().built_outposts().begin()->first;
            ok(w,ship,5,Action::ConstructWorkshop,site);
            ok(w,ship,6,Action::ConstructStorage,site);
            // Links need the actual constructed workshop and an existing container.
            CHECK(issue(w,stranger,7,Action::ConstructWorkshopLink,site,4*7,0,0,8).result!=Outcome::Applied);
            CHECK(issue(w,ship,8,Action::ConstructWorkshopLink,site,4*7,1,0).result==Outcome::InvalidTarget);
            CHECK(issue(w,ship,800,Action::ConstructWorkshopLink,site,4*7+2,0,1).result==Outcome::InvalidTarget); // no finisher
            Command invalid{};invalid.id=801;invalid.sequence=801;invalid.issuer=7;
            invalid.actor=ship;invalid.target=site;invalid.action=Action::ConstructWorkshopLink;
            invalid.amount=4*7+1;invalid.thrust_y=0;invalid.execute_tick=w.world().tick()+1;
            CHECK(!w.submit(invalid)); // only aluminum can enter workshop via link
            ok(w,ship,9,Action::ConstructUtility,site);
            ok(w,ship,10,Action::ConstructHabitat,site);
            for(int n=1;n<=3;++n)ok(w,ship,10+n,Action::TransferHabitatStock,site,500,n);
            ok(w,ship,14,Action::AssignHabitatResidents,site,2);
            ok(w,ship,15,Action::AssignUtilityTechnicians,site,1);
            ok(w,ship,16,Action::TransferUtilitySpares,site,5);
            ok(w,ship,17,Action::SetUtilityMaintenance,site,1);
            std::uint64_t worker=0;
            for(const auto& [id,n]:w.world().npc_records())
                if(n.location==site && n.role==NpcRole::Habitat){worker=id;break;}
            CHECK(worker!=0);
            ok(w,ship,18,Action::AssignWorkshopOperator,site,static_cast<std::int64_t>(worker));
            // Input links cannot make material while the real bin is empty.
            const auto modules_before=w.world().colony_ship(ship)->modules;
            ok(w,ship,19,Action::ConstructWorkshopLink,site,4*7,0,0); // aluminum -> machine
            CHECK(w.world().colony_ship(ship)->modules==modules_before-1);
            CHECK(w.world().storage_link(site,19)->destination==kWorkshopConveyorNode);
            w.steps(12);
            CHECK(w.world().storage_link(site,19)->units_moved==0);
            CHECK(w.world().workshop(site)->completed_batches==0);
            CHECK(w.world().workshop(site)->scrap_kg==0);
            CHECK(issue(w,ship,20,Action::ConstructWorkshopLink,site,4*7,0,0).result==Outcome::InvalidTarget); // duplicate
            ok(w,ship,21,Action::ConstructWorkshopLink,site,4*7+1,0,1); // panels -> container
            ok(w,ship,22,Action::ConstructWorkshopLink,site,4*7+3,0,1); // scrap -> container
            CHECK(w.world().storage_link(site,21)->source==kWorkshopConveyorNode);
            CHECK(w.world().storage_link(site,22)->source==kWorkshopConveyorNode);
            ok(w,ship,23,Action::Mine,silicate,30);
            ok(w,ship,24,Action::RefineMineral,10,12000);
            ok(w,ship,25,Action::TransferStorageStack,site,1000,0,0);
            CHECK(w.world().storage_inventory_snapshot(site).amount("good/aluminum")==993); // first link tick
            CHECK(w.world().total_material_kg()==conservation);
            w.steps(90);
            const auto& workshop=*w.world().workshop(site);
            CHECK(workshop.completed_batches>0);
            CHECK(w.world().storage_link(site,19)->units_moved>0);
            CHECK(w.world().storage_link(site,21)->units_moved>0);
            CHECK(w.world().storage_link(site,22)->units_moved>0);
            CHECK(w.world().storage_inventory_snapshot(site).amount("good/aluminum_panels")>0);
            CHECK(w.world().storage_inventory_snapshot(site).amount("good/aluminum_scrap")>0);
            CHECK(w.world().total_material_kg()==conservation);
            saved=w.world().canonical_state();
            CHECK(saved.rfind("galaxy-kernel-v28|",0)==0);
            CHECK(Simulation::from_snapshot(saved).canonical_state()==saved);
            // Remote ordinary-step accounting must match sequential steps exactly.
            auto independent=Simulation::from_snapshot(saved);
            w.steps(40);independent.steps(40);
            CHECK(w.world().canonical_state()==independent.canonical_state());
            CHECK(w.world().total_material_kg()==conservation);
            saved=w.world().canonical_state();
            // Valid WIP and conveyor transfer effects must be durable.
        }
        {
            PersistentWorld w(db,Simulation(3));
            CHECK(w.world().canonical_state()==saved);
            CHECK(w.world().total_material_kg()==conservation);
            CHECK(w.world().storage_link(site,21)->source==kWorkshopConveyorNode);
            const auto before=w.world().canonical_state();
            bool failed=false;
            try{w.step(FailPoint::AfterSnapshotWrite);}catch(const std::exception&){failed=true;}
            CHECK(failed);
            CHECK(w.world().canonical_state()==before);
        }
        {
            PersistentWorld w(db,Simulation(4));
            CHECK(w.world().canonical_state()==saved);
        }
        sqlite3* dbh=nullptr;CHECK(sqlite3_open(db.c_str(),&dbh)==SQLITE_OK);
        // Forging a factory-side transfer endpoint must fail on restart.
        CHECK(sqlite3_exec(dbh,
            "UPDATE resource_ledger SET source_id='forged' WHERE id=(SELECT MIN(id) FROM resource_ledger WHERE reason='automated_storage_link_transfer' AND source_id LIKE 'workshop:%')",
            nullptr,nullptr,nullptr)==SQLITE_OK);
        sqlite3_close(dbh);
        bool detected=false;
        try{PersistentWorld forged(db,Simulation(5));(void)forged;}catch(const std::exception&){detected=true;}
        CHECK(detected);
        fs::remove_all(dir);
        std::cout<<"ECON-02B2d2 real workshop conveyor inputs/outputs, WIP, conservation, recovery PASS\n";
        return 0;
    }catch(const std::exception& ex){
        std::cerr<<"ECON-02B2d2 FAILED "<<ex.what()<<'\n';
        fs::remove_all(dir);return 1;
    }
}
