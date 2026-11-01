#include "galaxy/HostBlueprintRegistry.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
using namespace galaxy::industry;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("B2d4c1 line ")+std::to_string(__LINE__)+": " #x);}while(0)
template<typename F> static void reject(F f){bool threw=false;try{f();}catch(const std::exception&){threw=true;}CHECK(threw);}
static Event issue(PersistentWorld& w,EntityId ship,std::uint64_t id,Action action,
       EntityId target=0,std::int64_t amount=1){
    Command c{};c.id=id;c.sequence=id;c.issuer=7;c.actor=ship;c.target=target;
    c.action=action;c.amount=amount;c.execute_tick=w.world().tick()+1;
    CHECK(w.submit(c));w.step();return w.world().events().back();
}
static void ok(PersistentWorld& w,EntityId ship,std::uint64_t id,Action action,
      EntityId target=0,std::int64_t amount=1){CHECK(issue(w,ship,id,action,target,amount).result==Outcome::Applied);}
static void tamper(const std::string& path,const std::string& sql){
    sqlite3* db=nullptr;CHECK(sqlite3_open(path.c_str(),&db)==SQLITE_OK);
    char* error=nullptr;const int rc=sqlite3_exec(db,sql.c_str(),nullptr,nullptr,&error);
    if(rc!=SQLITE_OK){std::string what=error?error:"sqlite error";sqlite3_free(error);sqlite3_close(db);throw std::runtime_error(what);}
    sqlite3_close(db);
}
int main(){
    auto dir=fs::temp_directory_path()/("econ02b2d4c1_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);const auto db=(dir/"world.sqlite").string();
    try{
        Simulation world(40042);
        const auto ship=world.add_colony_ship(7,{0,0},400,8);
        const auto rogue=world.add_colony_ship(8,{0,0},200,4);
        world.provision_colony_genesis(ship,1000,3000,1000,1000,1000);
        const auto asteroid=world.add_asteroid({1000,0},120);
        world.enable_individual_workforce();
        const auto origin=world.total_material_kg();
        EntityId site=0;std::int64_t plan_id=0;std::int64_t next_plan=0;
        std::uint64_t last_id=0;std::size_t saved=0;
        // Consume the exact .sglayout exported by the offline browser editor,
        // not a C++ fixture assembled specially for this host test.
        std::ifstream design_file(SG_EDITOR_FIXTURE_PATH);
        CHECK(bool(design_file));
        const std::string design((std::istreambuf_iterator<char>(design_file)),{});
        CHECK(FactoryLayout::from_blueprint(design).canonical_blueprint()==design);
        std::string persisted;
        {
            PersistentWorld w(db,std::move(world));
            ok(w,ship,1,Action::Mine,asteroid,24);
            ok(w,ship,2,Action::RefineOre,0,24);
            ok(w,ship,3,Action::FabricateModule,0,12);
            ok(w,ship,4,Action::ConstructOutpost);
            site=w.world().built_outposts().begin()->first;
            HostBlueprintRegistry host(db,w);
            reject([&]{host.register_layout(design,8,rogue,site);});
            reject([&]{host.register_layout(design+"extra",7,ship,site);});
            reject([&]{host.register_layout("{\"format\":\"single-galaxy-factory-design-v1\"}",7,ship,site);});
            plan_id=host.register_layout(design,7,ship,site);
            CHECK(plan_id>0);
            reject([&]{(void)host.status(plan_id,8);});
            reject([&]{(void)host.execute_one(plan_id,8);});
            reject([&]{(void)host.cancel(plan_id,8);});
            // The same site must have only one active design authority.
            reject([&]{(void)host.register_layout(design,7,ship,site);});
            const auto start=host.status(plan_id,7);
            CHECK(start.progress.total==16 && start.progress.completed==0 && start.progress.modules_remaining==11);
            for(int i=0;i<6;i++){
                auto e=host.execute_one(plan_id,7);CHECK(e.result==Outcome::Applied);
                CHECK(e.command_id>500000000000000000ULL && e.command_id>last_id);
                last_id=e.command_id;
            }
            auto status=host.status(plan_id,7);
            CHECK(status.progress.completed==6 && !status.progress.cancelled);
            saved=status.progress.completed;
            CHECK(w.world().total_material_kg()==origin);
            persisted=w.world().canonical_state();
        }
        {
            PersistentWorld w(db,Simulation(9));
            CHECK(w.world().canonical_state()==persisted);
            HostBlueprintRegistry host(db,w);
            CHECK(host.status(plan_id,7).progress.completed==saved);
            for(int i=0;i<3;i++){
                auto e=host.execute_one(plan_id,7);CHECK(e.result==Outcome::Applied);
                CHECK(e.command_id>last_id);last_id=e.command_id;
            }
            const auto built=w.world().colony_ship(ship)->modules;
            CHECK(host.cancel(plan_id,7));
            CHECK(!host.cancel(plan_id,7));
            CHECK(host.status(plan_id,7).progress.cancelled);
            reject([&]{(void)host.execute_one(plan_id,7);});
            CHECK(w.world().colony_ship(ship)->modules==built);
            CHECK(w.world().total_material_kg()==origin);
            // A newly authorized immutable revision can recover work already
            // constructed. The old cancellation remains irreversible.
            next_plan=host.register_layout(design,7,ship,site);
            CHECK(next_plan>plan_id && !host.status(next_plan,7).progress.cancelled);
            CHECK(host.status(next_plan,7).progress.completed==9);
            reject([&]{(void)host.register_layout(design,7,ship,site);});
            bool restocked=false;
            for(int i=0;i<24 && !host.status(next_plan,7).progress.finished();i++){
                const auto e=host.execute_one(next_plan,7);
                CHECK(e.command_id>last_id);last_id=e.command_id;
                if(e.result==Outcome::NoResources){
                    CHECK(!restocked);
                    CHECK(w.world().total_material_kg()==origin);
                    // Replenishment goes through real mining/refining/fabrication.
                    ok(w,ship,80,Action::Mine,asteroid,40);
                    ok(w,ship,81,Action::RefineOre,0,40);
                    ok(w,ship,82,Action::FabricateModule,0,20);
                    restocked=true;
                }else CHECK(e.result==Outcome::Applied);
            }
            CHECK(host.status(next_plan,7).progress.finished());
            CHECK(w.world().storage(site)->links.size()==2);
            CHECK(w.world().total_material_kg()==origin);
            persisted=w.world().canonical_state();
        }
        {
            PersistentWorld w(db,Simulation(10));HostBlueprintRegistry host(db,w);
            CHECK(w.world().canonical_state()==persisted);
            CHECK(host.status(plan_id,7).progress.cancelled);
            CHECK(host.status(next_plan,7).progress.finished());
            reject([&]{(void)host.execute_one(plan_id,7);});
            reject([&]{(void)host.execute_one(next_plan,7);});
        }
        // Direct SQLite edits must not silently rewrite the host-approved plan.
        tamper(db,"UPDATE sg_host_plans SET manifest='attacker-supplied-blueprint' WHERE id=1");
        {
            PersistentWorld w(db,Simulation(11));HostBlueprintRegistry host(db,w);
            reject([&]{(void)host.status(plan_id,7);});
            reject([&]{(void)host.execute_one(plan_id,7);});
            CHECK(host.status(next_plan,7).progress.finished());
        }
        tamper(db,"UPDATE sg_host_plans SET cancelled=0 WHERE id=1");
        {
            PersistentWorld w(db,Simulation(12));HostBlueprintRegistry host(db,w);
            reject([&]{(void)host.status(plan_id,7);});
        }
        fs::remove_all(dir);
        std::cout<<"ECON-02B2d4c1 hosted durable plan registration/revocation/recovery PASS\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"ECON-02B2d4c1 FAIL: "<<e.what()<<'\n';fs::remove_all(dir);return 1;}
}
