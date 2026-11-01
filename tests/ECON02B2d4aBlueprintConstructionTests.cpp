#include "galaxy/BlueprintConstruction.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
using namespace galaxy::industry;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("B2d4a line ")+std::to_string(__LINE__)+": " #x);}while(0)
static Event issue(PersistentWorld& w,EntityId ship,std::uint64_t id,Action action,
                   EntityId target=0,std::int64_t amount=1,int x=0,int y=0){
    Command c{};c.id=id;c.sequence=id;c.issuer=7;c.actor=ship;c.target=target;
    c.action=action;c.amount=amount;c.thrust_x=x;c.thrust_y=y;c.execute_tick=w.world().tick()+1;
    CHECK(w.submit(c));w.step();return w.world().events().back();
}
static void ok(PersistentWorld& w,EntityId ship,std::uint64_t id,Action a,
               EntityId target=0,std::int64_t amount=1,int x=0,int y=0){
    const auto e=issue(w,ship,id,a,target,amount,x,y);if(e.result!=Outcome::Applied)
      std::cerr<<"Action "<<static_cast<int>(a)<<" outcome "<<static_cast<int>(e.result)<<'\n';
    CHECK(e.result==Outcome::Applied);
}
static FactoryLayout blueprint(){
    FactoryLayout bp;
    bp.place_storage(1,{0,0},1,1,4000); // original site container
    bp.place_storage(2,{4,0},1,1,4000); // new material-backed container
    bp.place_machine(kWorkshopConveyorNode,{8,0},1,{"good/aluminum"},
      {"good/aluminum_scrap","good/aluminum_frames","good/aluminum_panels"});
    bp.add_conveyor(50,1,2,"good/aluminum",7,{{1,0},{2,0},{3,0}});
    bp.add_conveyor(51,2,kWorkshopConveyorNode,"good/aluminum",5,{{5,0},{6,0},{7,0}});
    return bp;
}
int main(){
    auto dir=fs::temp_directory_path()/("econ02b2d4a_"+
           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);const auto db=(dir/"world.sqlite").string();
    try{
        auto bp=blueprint();
        CHECK(FactoryLayout::from_blueprint(bp.canonical_blueprint()).canonical_blueprint()==bp.canonical_blueprint());
        bool failed=false;
        try{FactoryLayout wrong;wrong.place_storage(1,{0,0},1,1,4000);wrong.place_junction(2,DeviceKind::Splitter,{2,0});
            (void)BlueprintConstruction(std::move(wrong),1,2,7,100000);}catch(...){failed=true;}
        CHECK(failed);
        Simulation start(40042);
        auto ship=start.add_colony_ship(7,{0,0},400,8);
        auto rogue=start.add_colony_ship(8,{0,0},200,4);
        start.provision_colony_genesis(ship,1000,3000,1000,1000,1000);
        auto rock=start.add_asteroid({1000,0},120);
        auto geology=start.add_geological_asteroid({1000,0},AsteroidClass::Silicate,30,2500);
        start.enable_individual_workforce();
        const auto origin=start.total_material_kg();
        EntityId site=0;
        std::size_t saved_steps=0;
        std::int64_t modules_at_checkpoint=0;
        std::string snapshot;
        {
            PersistentWorld w(db,std::move(start));
            ok(w,ship,1,Action::Mine,rock,24);
            ok(w,ship,2,Action::RefineOre,0,24);
            ok(w,ship,3,Action::FabricateModule,0,12);
            ok(w,ship,4,Action::ConstructOutpost);
            site=w.world().built_outposts().begin()->first;
            auto plan=BlueprintConstruction(bp,site,ship,7,100000);
            CHECK(plan.jobs().size()==16);
            CHECK(plan.inspect(w.world()).modules_remaining==11);
            auto data=plan.manifest();
            {std::ofstream file(dir/"factory.construction");file<<data;}
            CHECK(BlueprintConstruction::from_manifest(data).manifest()==data);
            auto cancelled=BlueprintConstruction::from_manifest(data);
            cancelled.cancel();
            CHECK(cancelled.cancelled());
            CHECK(BlueprintConstruction::from_manifest(cancelled.manifest()).cancelled());
            bool denied=false;
            try{(void)cancelled.execute_next(w,7,ship,90000);}catch(...){denied=true;}
            CHECK(denied);
            CHECK(w.world().colony_ship(ship)->modules==8);
            failed=false;try{(void)BlueprintConstruction::from_manifest(data+"extra");}catch(...){failed=true;}CHECK(failed);
            CHECK(plan.jobs()[0].command.action==Action::ConstructStorage);
            // Stage 1: material-backed construction + placements + part of a path.
            for(int i=0;i<8;i++){
                const auto event=plan.execute_next(w,7,ship,300000+w.world().tick());
                CHECK(event.result==Outcome::Applied);
                CHECK(w.world().total_material_kg()==origin);
            }
            CHECK(w.world().storage(site)->extra_bins.count(1));
            CHECK(w.world().workshop(site));
            CHECK(w.world().storage(site)->links.begin()->second.route_cells.size()==1);
            CHECK(!w.world().storage(site)->links.begin()->second.route_ready);
            auto status=plan.inspect(w.world());CHECK(status.completed==8 && status.modules_remaining==6);
            saved_steps=status.completed;
            modules_at_checkpoint=w.world().colony_ship(ship)->modules;
            snapshot=w.world().canonical_state();
        }
        {
            // The plan manifest is a user-owned saved document; authoritative
            // progress comes from the restored SQLite world, NEVER the document.
            std::ifstream f(dir/"factory.construction");std::string file((std::istreambuf_iterator<char>(f)),{});
            auto plan=BlueprintConstruction::from_manifest(file);
            PersistentWorld w(db,Simulation(3));
            CHECK(w.world().canonical_state()==snapshot);
            CHECK(plan.inspect(w.world()).completed==saved_steps);
            CHECK(plan.inspect(w.world()).modules_remaining==6);
            failed=false;try{(void)plan.execute_next(w,8,rogue,300000+w.world().tick());}catch(...){failed=true;}CHECK(failed);
            CHECK(w.world().colony_ship(ship)->modules==modules_at_checkpoint);
            // Remaining modules are insufficient (only 1 after 8 steps);
            // a blocked job must not consume materials or advance plan progress.
            for(int i=0;i<4;i++){
                const auto e=plan.execute_next(w,7,ship,300000+w.world().tick());
                CHECK(e.result==Outcome::Applied);
            }
            CHECK(plan.inspect(w.world()).completed==12);
            CHECK(w.world().colony_ship(ship)->modules==0);
            const auto before=w.world().total_material_kg();
            const auto blocked=plan.execute_next(w,7,ship,300000+w.world().tick());
            CHECK(blocked.result==Outcome::NoResources);
            CHECK(plan.inspect(w.world()).completed==12);
            CHECK(w.world().total_material_kg()==before);
            // Actual supplies arrive through the existing mining/refining/fabrication
            // chain, and the saved blueprint resumes without duplicating old jobs.
            ok(w,ship,80,Action::Mine,rock,40);
            ok(w,ship,81,Action::RefineOre,0,40);
            ok(w,ship,82,Action::FabricateModule,0,20);
            auto rebooted=BlueprintConstruction::from_manifest(file);
            for(int i=0;i<20 && !rebooted.inspect(w.world()).finished();i++){
                const auto e=rebooted.execute_next(w,7,ship,300000+w.world().tick());
                CHECK(e.result==Outcome::Applied);
            }
            const auto finished=rebooted.inspect(w.world());
            CHECK(finished.finished() && finished.modules_remaining==0);
            CHECK(w.world().storage(site)->links.size()==2);
            const auto one=w.world().storage_link(site,w.world().storage(site)->links.begin()->first);
            CHECK(one && one->route_ready && one->route_cells.size()==3);
            CHECK(w.world().total_material_kg()==origin);
            CHECK(Simulation::from_snapshot(w.world().canonical_state()).canonical_state()==w.world().canonical_state());
            // A completed construction cannot be replayed to spend materials twice.
            failed=false;try{(void)rebooted.execute_next(w,7,ship,300000+w.world().tick());}catch(...){failed=true;}CHECK(failed);
            // Feed a real inventory after construction; both transport links
            // remain the same authoritative rate-limited system as before.
            ok(w,ship,83,Action::Mine,geology,45);
            ok(w,ship,84,Action::RefineMineral,10,18000);
            ok(w,ship,85,Action::TransferStorageStack,site,100,0,0);
            CHECK(w.world().storage_inventory_snapshot(site).amount("good/aluminum")<100);
            CHECK(w.world().total_material_kg()==origin);
            snapshot=w.world().canonical_state();
        }
        {
            PersistentWorld w(db,Simulation(66));
            CHECK(w.world().canonical_state()==snapshot);
            std::ifstream f(dir/"factory.construction");std::string file((std::istreambuf_iterator<char>(f)),{});
            CHECK(BlueprintConstruction::from_manifest(file).inspect(w.world()).finished());
            CHECK(w.world().total_material_kg()==origin);
        }
        fs::remove_all(dir);
        std::cout<<"ECON-02B2d4a blueprint-driven durable construction and staged recovery PASS\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"ECON-02B2d4a FAIL: "<<e.what()<<'\n';fs::remove_all(dir);return 1;
    }
}
