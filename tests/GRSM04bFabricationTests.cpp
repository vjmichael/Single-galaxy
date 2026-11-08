#include "galaxy/Persistence.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do { if(!(x)) throw std::runtime_error("GRSM04b.B2a line "+std::to_string(__LINE__)+": " #x); } while(0)
struct Temp {
    fs::path dir;
    Temp():dir(fs::temp_directory_path()/("grsm04bB2a_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))){fs::create_directories(dir);}
    ~Temp(){std::error_code ec;fs::remove_all(dir,ec);}
};
void sql(const fs::path& path,const std::string& script){
    sqlite3* db=nullptr;CHECK(sqlite3_open(path.string().c_str(),&db)==SQLITE_OK);
    char* err=nullptr;const auto rc=sqlite3_exec(db,script.c_str(),nullptr,nullptr,&err);
    const std::string msg(err?err:"");sqlite3_free(err);sqlite3_close(db);
    if(rc!=SQLITE_OK)throw std::runtime_error("fixture SQL: "+msg);
}
bool rejects(const fs::path& p){
    try { PersistentWorld w(p.string(),Simulation(1)); return false; }
    catch(const std::runtime_error&){return true;}
}
void queue(PersistentWorld& w,std::uint64_t id,EntityId ship,EntityId target,Action action,std::int64_t qty){
    Command c{};c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
    c.issuer=7;c.actor=ship;c.target=target;c.action=action;c.amount=qty;
    CHECK(w.submit(c));
}
void real_named_fabrication(const fs::path& dir){
    const auto file=dir/"real.sqlite", copy=dir/"recovered.sqlite";
    Simulation s(84);const auto ship=s.add_colony_ship(7,{0,0},50,5);
    const auto other=s.add_colony_ship(8,{50000,0},50,3);
    const auto rock=s.add_asteroid({1000,0},60);
    s.enable_individual_workforce();
    {
        PersistentWorld w(file.string(),std::move(s));
        CHECK(!w.submit_npc_fabrication_work(6,ship,Action::FabricateModule,0,1,41));
        CHECK(!w.submit_npc_fabrication_work(999,ship,Action::FabricateModule,0,1,41));
        CHECK(!w.submit_npc_fabrication_work(1,other,Action::FabricateModule,0,1,41));
        CHECK(!w.submit_npc_fabrication_work(1,ship,Action::Mine,0,1,41));
        CHECK(!w.submit_npc_fabrication_work(1,ship,Action::FabricateModule,1,1,41));
        CHECK(!w.submit_npc_fabrication_work(1,ship,Action::FabricateMaterialModule,3,1,41));
        CHECK(!w.submit_npc_fabrication_work(1,ship,Action::FabricateModule,0,0,41));
        CHECK(w.recent_npc_fabrication_assignments().empty());
        queue(w,10,ship,rock,Action::Mine,10);w.step();
        queue(w,11,ship,0,Action::RefineOre,10);w.step();
        CHECK(w.world().colony_ship(ship)->refined==10);
        const auto state=w.world().canonical_state();
        try{w.submit_npc_fabrication_work(1,ship,Action::FabricateModule,0,2,41,FailPoint::AfterReceiptWrite);CHECK(false);}
        catch(const std::runtime_error&){}
        CHECK(state==w.world().canonical_state());
        CHECK(w.recent_npc_fabrication_assignments().empty());
        CHECK(w.submit_npc_fabrication_work(1,ship,Action::FabricateModule,0,2,41));
        CHECK(!w.submit_npc_fabrication_work(1,ship,Action::FabricateModule,0,1,42)); // one active order
        CHECK(!w.submit_npc_mining_work(1,ship,rock,1,43)); // also blocked across job types
        CHECK(w.submit_npc_mining_work(2,ship,rock,1,44)); // a different worker can operate
        CHECK(w.recent_npc_fabrication_assignments().size()==1);
        CHECK(w.recent_npc_fabrication_executions().empty());
        w.step();
        CHECK(w.world().colony_ship(ship)->modules==2);
        CHECK(w.world().colony_ship(ship)->refined==6);
        CHECK(w.world().total_ore()==60);
        CHECK(w.verified_npc_mining_execution(44,2));
        CHECK(w.verified_npc_fabrication_execution(41,1));
        CHECK(!w.verified_npc_fabrication_execution(41,2));
        const auto e=w.recent_npc_fabrication_executions();
        CHECK(e.size()==1&&e[0].transferred==4&&e[0].npc_id==1&&e[0].command_id==41);
        CHECK(e[0].incident_id.rfind("cargo:",0)==0);
        const auto a=w.recent_npc_fabrication_assignments();
        CHECK(a.size()==1&&a[0].issuer==7&&a[0].action==Action::FabricateModule&&a[0].quantity==2);
        // A regular owner's fabrication is not proof that any particular NPC worked.
        queue(w,45,ship,0,Action::FabricateModule,1);w.step();
        CHECK(w.world().colony_ship(ship)->modules==3);
        CHECK(w.recent_npc_fabrication_executions().size()==1);
        CHECK(!w.verified_npc_fabrication_execution(45,1));
        // An assigned but unfundable order is recorded as failed, never executed.
        CHECK(w.submit_npc_fabrication_work(3,ship,Action::FabricateModule,0,50,46));
        w.step();
        CHECK(w.recent_npc_fabrication_assignments().size()==2);
        CHECK(w.recent_npc_fabrication_executions().size()==1);
        CHECK(!w.verified_npc_fabrication_execution(46,3));
        // An ordinary mining result + real mineral refinement gives a second
        // path: aluminum can become a named module using only actual feedstock.
        // For isolation, the lack of usable aluminum produces no false credit.
        CHECK(w.submit_npc_fabrication_work(4,ship,Action::FabricateMaterialModule,2,1,47));
        w.step();
        CHECK(!w.verified_npc_fabrication_execution(47,4));
        PersistentWorld::recover_copy(file.string(),copy.string());
    }
    for(const auto& p:{file,copy}){
        PersistentWorld w(p.string(),Simulation(999));
        CHECK(w.recent_npc_fabrication_assignments().size()==3);
        CHECK(w.recent_npc_fabrication_executions().size()==1);
        CHECK(w.verified_npc_fabrication_execution(41,1));
        CHECK(w.verified_npc_mining_execution(44,2));
        CHECK(!w.verified_npc_fabrication_execution(46,3));
        CHECK(w.world().colony_ship(ship)->modules==3);
    }
    const std::vector<std::pair<std::string,std::string>> attacks{
        {"fake_worker","DROP TRIGGER npc_fabrication_execution_no_update; UPDATE npc_fabrication_executions SET npc_id='2' WHERE id=1;"},
        {"fake_issuer","DROP TRIGGER npc_fabrication_assignment_no_update; UPDATE npc_fabrication_assignments SET issuer='8' WHERE id=1;"},
        {"fake_amount","DROP TRIGGER npc_fabrication_execution_no_update; UPDATE npc_fabrication_executions SET transferred=100 WHERE id=1;"},
        {"wrong_material","DROP TRIGGER npc_fabrication_assignment_no_update; UPDATE npc_fabrication_assignments SET material_code=2 WHERE id=1;"},
        {"fake_incident","PRAGMA foreign_keys=OFF; DROP TRIGGER npc_fabrication_execution_no_update; UPDATE npc_fabrication_executions SET incident_id='cargo:000000' WHERE id=1;"},
        {"deleted_execution","DROP TRIGGER npc_fabrication_execution_no_delete; DELETE FROM npc_fabrication_executions WHERE id=1;"},
        {"bad_tip","UPDATE npc_fabrication_execution_meta SET tip='other' WHERE singleton=1;"},
        {"bad_count","UPDATE npc_fabrication_assignment_meta SET event_count=99 WHERE singleton=1;"}
    };
    for(const auto& [name,attack]:attacks){
        const auto forged=dir/(name+".sqlite");fs::copy_file(file,forged);
        sql(forged,attack);CHECK(rejects(forged));
    }
}
void mineral_fed_fabrication(const fs::path& dir){
    const auto file=dir/"minerals.sqlite";
    Simulation s(91);const auto ship=s.add_colony_ship(7,{0,0},150,6);
    const auto rock=s.add_geological_asteroid({1000,0},AsteroidClass::Silicate,30,2500);
    s.enable_individual_workforce();
    const auto original=s.total_material_kg();
    {
        PersistentWorld w(file.string(),std::move(s));
        queue(w,901,ship,rock,Action::Mine,30);w.step();
        CHECK(w.world().colony_ship(ship)->minerals_kg.at("plagioclase")==12000);
        queue(w,902,ship,10,Action::RefineMineral,12000);w.step();
        CHECK(w.world().colony_ship(ship)->goods_kg.at("aluminum")==2400);
        CHECK(w.submit_npc_fabrication_work(1,ship,Action::FabricateMaterialModule,2,1,903));
        w.step();
        CHECK(w.verified_npc_fabrication_execution(903,1));
        CHECK(w.world().colony_ship(ship)->goods_kg.at("aluminum")==400);
        CHECK(w.world().colony_ship(ship)->modules==1);
        CHECK(w.world().total_material_kg()==original);
        CHECK(w.recent_npc_fabrication_executions().at(0).transferred==2);
    }
    PersistentWorld w(file.string(),Simulation(100));
    CHECK(w.verified_npc_fabrication_execution(903,1));
    CHECK(w.world().total_material_kg()==original);
}
void migration_16_to_17(const fs::path& dir){
    const auto file=dir/"migrate.sqlite";
    Simulation s(31);const auto ship=s.add_colony_ship(7,{0,0},50,3);s.enable_individual_workforce();
    {
        PersistentWorld w(file.string(),std::move(s));
        CHECK(!w.verified_npc_fabrication_execution(555,1));
    }
    sql(file,"DROP TABLE npc_fabrication_executions; DROP TABLE npc_fabrication_execution_meta; "
        "DROP TABLE npc_fabrication_assignments; DROP TABLE npc_fabrication_assignment_meta; DROP TABLE IF EXISTS sg_native_npc_commands; PRAGMA user_version=16;");
    {
        PersistentWorld w(file.string(),Simulation(99));
        CHECK(w.recent_npc_fabrication_assignments().empty());
        CHECK(w.recent_npc_fabrication_executions().empty());
        CHECK(w.submit_npc_fabrication_work(1,ship,Action::FabricateModule,0,1,555));
        w.step(); // no refined feedstock; fails safely
        CHECK(!w.verified_npc_fabrication_execution(555,1));
    }
    PersistentWorld w(file.string(),Simulation(55));
    CHECK(w.recent_npc_fabrication_assignments().size()==1);
    CHECK(w.recent_npc_fabrication_executions().empty());
}
int main(){Temp t;try{
    real_named_fabrication(t.dir);std::cout<<"PASS B2a named fabrication, segregation, rollback, recovery, tamper\n";
    mineral_fed_fabrication(t.dir);std::cout<<"PASS real silicate -> aluminum -> named material module\n";
    migration_16_to_17(t.dir);std::cout<<"PASS v16 -> v17 without retroactive worker credit\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"GRSM04b.B2a FAIL: "<<e.what()<<'\n';return 1;}}
