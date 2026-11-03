#include "galaxy/Persistence.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do {if(!(x))throw std::runtime_error(std::string("B2c3 line ")+std::to_string(__LINE__)+": " #x);} while(0)
static Event issue(PersistentWorld& w, EntityId ship, std::uint64_t id, Action action,
                  EntityId site=0, std::int64_t amount=1, int x=0,int y=0, PlayerId issuer=7){
    Command c{};c.id=id;c.sequence=id;c.issuer=issuer;c.actor=ship;
    c.action=action;c.target=site;c.amount=amount;c.thrust_x=x;c.thrust_y=y;
    c.execute_tick=w.world().tick()+1;
    if(!w.submit(c))throw std::runtime_error("submit rejected id="+std::to_string(id)+" action="+std::to_string(static_cast<int>(action)));w.step();return w.world().events().back();
}
static void ok(PersistentWorld& w, EntityId ship,std::uint64_t id,Action action,
               EntityId site=0,std::int64_t amount=1,int x=0,int y=0){
    CHECK(issue(w,ship,id,action,site,amount,x,y).result==Outcome::Applied);
}
static std::int64_t amount(const PersistentWorld& w,EntityId site,unsigned node,const std::string& item){
    return w.world().storage_node_inventory_snapshot(site,node).amount("good/"+item);
}
int main(){
    const auto dir=fs::temp_directory_path()/ ("econ02b2c3_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    auto db=(dir/"world.sqlite").string();
    try {
        Simulation genesis(8823);
        const auto ship=genesis.add_colony_ship(7,{0,0},400,8);
        genesis.provision_colony_genesis(ship,1000,3000,1000,1000,1000);
        const auto rock=genesis.add_asteroid({1000,0},100);
        const auto silicate=genesis.add_geological_asteroid({1000,0},AsteroidClass::Silicate,30,2500);
        const auto stranger=genesis.add_colony_ship(8,{0,0},50,4);
        genesis.enable_individual_workforce();
        const auto total=genesis.total_material_kg();
        EntityId site=0;
        std::string before_restart, before_fault;
        {
            PersistentWorld w(db,std::move(genesis));
            ok(w,ship,1,Action::Mine,rock,80);
            ok(w,ship,2,Action::RefineOre,0,80);
            ok(w,ship,3,Action::FabricateModule,0,40);
            ok(w,ship,4,Action::ConstructOutpost);
            site=w.world().built_outposts().begin()->first;
            ok(w,ship,5,Action::ConstructStorage,site);
            ok(w,ship,6,Action::ConstructStorageNode,site,1);
            ok(w,ship,7,Action::ConstructStorageNode,site,2);
            CHECK(issue(w,stranger,8,Action::ConstructStorageLink,site,4*5,1,2,8).result!=Outcome::Applied);
            CHECK(issue(w,ship,9,Action::ConstructStorageLink,site,4*5,1,3).result==Outcome::InvalidTarget);
            ok(w,ship,10,Action::Mine,silicate,45);
            ok(w,ship,11,Action::RefineMineral,10,18000);
            ok(w,ship,12,Action::TransferStorageNodeStack,site,350,4,0);  // ship -> node 1
            CHECK(amount(w,site,1,"aluminum")==350);
            CHECK(amount(w,site,2,"aluminum")==0);
            const auto modules_before=w.world().colony_ship(ship)->modules;
            // The construction command is itself recorded; one transport tick
            // then moves exactly 7 units, not 350 and not a full pallet.
            ok(w,ship,13,Action::ConstructStorageLink,site,4*7,1,2);
            CHECK(w.world().colony_ship(ship)->modules==modules_before-1);
            CHECK(w.world().storage_link(site,13)!=nullptr);
            CHECK(w.world().storage_link(site,13)->units_moved==7);
            CHECK(amount(w,site,1,"aluminum")==343);
            CHECK(amount(w,site,2,"aluminum")==7);
            CHECK(w.world().total_material_kg()==total);
            // A directed cycle is forbidden, even if its item filter differs.
            CHECK(issue(w,ship,14,Action::ConstructStorageLink,site,4*7+1,2,1).result==Outcome::InvalidArgument);
            CHECK(w.world().storage_link(site,14)==nullptr);
            const auto before=w.world().canonical_state();
            auto parallel=Simulation::from_snapshot(before);
            w.steps(30);parallel.steps(30);
            CHECK(parallel.canonical_state()==w.world().canonical_state());
            CHECK(w.world().storage_link(site,13)->units_moved==7*32); // creation + refused cycle + 30 steps
            CHECK(w.world().total_material_kg()==total);
            // When source empties, repeated ticks add no phantom goods or scrap.
            w.steps(70);
            CHECK(amount(w,site,1,"aluminum")==0);
            CHECK(amount(w,site,2,"aluminum")==350);
            CHECK(w.world().storage_link(site,13)->units_moved==350);
            w.steps(4);
            CHECK(amount(w,site,2,"aluminum")==350);
            // A full destination causes upstream backpressure, even with stock in source.
            ok(w,ship,15,Action::Mine,silicate,45);
            ok(w,ship,16,Action::RefineMineral,10,18000);
            // Available ship aluminum from silicate easily supports these fills.
            ok(w,ship,170,Action::TransferStorageNodeStack,site,1000,8,0);
            ok(w,ship,171,Action::TransferStorageNodeStack,site,1000,8,0);
            ok(w,ship,172,Action::TransferStorageNodeStack,site,1000,8,0);
            ok(w,ship,173,Action::TransferStorageNodeStack,site,650,8,0); // fills node 2
            CHECK(w.world().storage_node_inventory_snapshot(site,2).free_liters()==0);
            ok(w,ship,18,Action::TransferStorageNodeStack,site,35,4,0); // source still stocked
            const auto moved=w.world().storage_link(site,13)->units_moved;
            w.steps(8);
            CHECK(amount(w,site,1,"aluminum")==35);
            CHECK(w.world().storage_link(site,13)->units_moved==moved);
            CHECK(w.world().total_material_kg()==total);
            // A filter for panels must not move aluminum from the same source.
            ok(w,ship,180,Action::ConstructStorageLink,site,4*7+1,1,0);
            CHECK(w.world().storage_link(site,180)->units_moved==0);
            CHECK(amount(w,site,1,"aluminum")==35);
            CHECK(amount(w,site,0,"aluminum")==0);
            w.steps(3);
            CHECK(w.world().storage_link(site,180)->units_moved==0);
            CHECK(w.world().total_material_kg()==total);
            CHECK(w.world().canonical_state().rfind("galaxy-kernel-v31|",0)==0);
            CHECK(Simulation::from_snapshot(w.world().canonical_state()).canonical_state()==w.world().canonical_state());
            before_restart=w.world().canonical_state();
        }
        {
            PersistentWorld restored(db,Simulation(2));
            CHECK(restored.world().canonical_state()==before_restart);
            CHECK(restored.world().storage_link(site,13)->units_moved==350);
            CHECK(restored.world().total_material_kg()==total);
            // Clear destination space with authorized ship transfer, resume link.
            ok(restored,ship,19,Action::TransferStorageNodeStack,site,80,8,1);
            CHECK(amount(restored,site,1,"aluminum")==28);
            CHECK(amount(restored,site,2,"aluminum")==3927);
            CHECK(restored.world().storage_link(site,13)->units_moved==357);
            CHECK(restored.world().total_material_kg()==total);
            before_fault=restored.world().canonical_state();
            // A mid-transaction write fault cannot commit a partial transport.
            bool failed=false;
            try{restored.step(FailPoint::AfterSnapshotWrite);}catch(const std::exception&){failed=true;}
            CHECK(failed);
            CHECK(restored.world().canonical_state()==before_fault);
        }
        {
            PersistentWorld recovered(db,Simulation(444));
            CHECK(recovered.world().canonical_state()==before_fault);
            CHECK(recovered.world().total_material_kg()==total);
        }
        sqlite3* raw=nullptr;CHECK(sqlite3_open(db.c_str(),&raw)==SQLITE_OK);
        const auto sql="UPDATE resource_ledger SET destination_id='forged' WHERE id="
                       "(SELECT MIN(id) FROM resource_ledger WHERE reason='automated_storage_link_transfer')";
        CHECK(sqlite3_exec(raw,sql,nullptr,nullptr,nullptr)==SQLITE_OK);
        sqlite3_close(raw);
        bool caught=false;
        try{PersistentWorld invalid(db,Simulation(3));(void)invalid;}catch(const std::exception&){caught=true;}
        CHECK(caught);
        fs::remove_all(dir);
        std::cout<<"ECON-02B2c3 durable authorized rate-limited links/backpressure PASS\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"ECON-02B2c3 FAIL "<<e.what()<<'\n';fs::remove_all(dir);return 1;
    }
}
