#include "galaxy/Persistence.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do {if(!(x))throw std::runtime_error("ECON02B1a line "+std::to_string(__LINE__)+": " #x);}while(0)

static Event send(PersistentWorld& w,EntityId ship,std::uint64_t id,Action action,
                  EntityId target=0,std::int64_t amt=1,int opt=0,PlayerId issuer=7){
    Command c{};c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
    c.issuer=issuer;c.actor=ship;c.target=target;c.action=action;
    c.amount=amt;c.thrust_x=opt;
    CHECK(w.submit(c));w.step();return w.world().events().back();
}
static void ok(PersistentWorld& w,EntityId ship,std::uint64_t id,Action action,
               EntityId target=0,std::int64_t amt=1,int opt=0){
    CHECK(send(w,ship,id,action,target,amt,opt).result==Outcome::Applied);
}
static bool malformed_snapshot_rejected(std::string state){
    try{(void)Simulation::from_snapshot(state);return false;}
    catch(const std::runtime_error&){return true;}
}
int main(){
    const auto dir=fs::temp_directory_path()/("econ02b1a_"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    const auto db=(dir/"real.sqlite").string();
    const auto recovery=(dir/"recovered.sqlite").string();
    EntityId ship=0,site=0;std::int64_t original_mass=0;
    try{
        Simulation s(92001);ship=s.add_colony_ship(7,{0,0},200,8);
        s.provision_colony_genesis(ship,1000,3000,1000,1000,1000);
        const auto bulk=s.add_asteroid({2000,0},150);
        const auto silicate=s.add_geological_asteroid({1000,0},AsteroidClass::Silicate,30,2500);
        s.enable_individual_workforce();
        original_mass=s.total_material_kg();
        {
            PersistentWorld w(db,std::move(s));
            ok(w,ship,1,Action::Mine,bulk,80);
            ok(w,ship,2,Action::RefineOre,0,80);
            ok(w,ship,3,Action::FabricateModule,0,20);
            ok(w,ship,4,Action::ConstructOutpost);
            site=w.world().built_outposts().begin()->first;
            CHECK(send(w,ship,5,Action::ConstructWorkshop,site,1,0,8).result==Outcome::Unauthorized);
            CHECK(w.world().workshop(site)==nullptr);
            ok(w,ship,6,Action::ConstructWorkshop,site);
            CHECK(w.world().workshop(site)->embodied_ore==2);
            CHECK(send(w,ship,7,Action::ConstructWorkshop,site).result==Outcome::InvalidTarget);
            CHECK(w.world().total_material_kg()==original_mass);
            // A real powered utility and physically relocated habitat resident.
            ok(w,ship,8,Action::ConstructUtility,site);
            ok(w,ship,9,Action::ConstructHabitat,site);
            for(int n=1;n<=3;++n)ok(w,ship,static_cast<std::uint64_t>(9+n),Action::TransferHabitatStock,site,100,n);
            ok(w,ship,13,Action::AssignHabitatResidents,site,2);
            ok(w,ship,14,Action::AssignUtilityTechnicians,site,1);
            ok(w,ship,15,Action::TransferUtilitySpares,site,5);
            ok(w,ship,16,Action::SetUtilityMaintenance,site,1);
            std::uint64_t worker=0;
            for(const auto& [id,n]:w.world().npc_records())if(n.location==site && n.role==NpcRole::Habitat){worker=id;break;}
            CHECK(worker!=0);
            CHECK(send(w,ship,17,Action::AssignWorkshopOperator,site,99999).result==Outcome::InvalidTarget);
            CHECK(send(w,ship,18,Action::TransferWorkshopFeed,site,1000).result==Outcome::NoResources);
            CHECK(w.world().workshop(site)->panels_kg==0 && w.world().workshop(site)->scrap_kg==0);
            // Mine, genuinely refine and physically deliver aluminum.
            ok(w,ship,19,Action::Mine,silicate,30);
            ok(w,ship,20,Action::RefineMineral,10,12000);
            CHECK(w.world().colony_ship(ship)->goods_kg.at("aluminum")==2400);
            ok(w,ship,21,Action::TransferWorkshopFeed,site,1000);
            CHECK(w.world().colony_ship(ship)->goods_kg.at("aluminum")==1400);
            CHECK(w.world().workshop(site)->aluminum_kg==1000);
            CHECK(w.world().workshop(site)->scrap_kg==0 && w.world().workshop(site)->panels_kg==0);
            CHECK(w.world().total_material_kg()==original_mass);
            // Operator designation starts a valid real WIP batch, not an instant output.
            ok(w,ship,22,Action::AssignWorkshopOperator,site,static_cast<std::int64_t>(worker));
            auto work=*w.world().workshop(site);
            CHECK(work.reserved_aluminum_kg==20 && work.panels_kg==0 && work.scrap_kg==0);
            CHECK(work.ticks_remaining==4 && work.aluminum_kg==980);
            CHECK(w.world().total_material_kg()==original_mass);
            PersistentWorld::recover_copy(db,recovery);
            // A valid reserved batch remains reserved after independent recovery.
            {
                PersistentWorld recovered(recovery,Simulation(999));
                CHECK(recovered.world().workshop(site)->reserved_aluminum_kg==20);
                CHECK(recovered.world().workshop(site)->ticks_remaining==4);
                recovered.steps(4);
                CHECK(recovered.world().workshop(site)->panels_kg==19);
                CHECK(recovered.world().workshop(site)->scrap_kg==1);
                CHECK(recovered.world().total_material_kg()==original_mass);
            }
            w.steps(4);
            work=*w.world().workshop(site);
            CHECK(work.completed_batches==1 && work.panels_kg==19 && work.scrap_kg==1);
            CHECK(work.reserved_aluminum_kg==0 && work.ticks_remaining==0);
            // Exactly fifty full cycles on the first 1t delivered batch.
            w.steps(245);
            work=*w.world().workshop(site);
            CHECK(work.completed_batches==50 && work.aluminum_kg==0);
            CHECK(work.panels_kg==950 && work.scrap_kg==50);
            CHECK(w.world().total_material_kg()==original_mass);
            w.steps(20); // no input: no extra scrap, output or work
            CHECK(w.world().workshop(site)->completed_batches==50);
            CHECK(w.world().workshop(site)->scrap_kg==50);
            // A second 1t shipment fills the finished bin after just two cycles.
            ok(w,ship,23,Action::TransferWorkshopFeed,site,1000);
            w.steps(20);
            work=*w.world().workshop(site);
            CHECK(work.completed_batches==52 && work.panels_kg==988 && work.scrap_kg==52);
            CHECK(work.aluminum_kg==960 && work.reserved_aluminum_kg==0);
            CHECK(w.world().total_material_kg()==original_mass);
            w.steps(60);
            CHECK(w.world().workshop(site)->completed_batches==52); // storage backpressure
            CHECK(w.world().workshop(site)->aluminum_kg==960);
            // One physical 1t pallet leaves, reopening machine capacity.
            ok(w,ship,24,Action::TransferWorkshopOutput,site,50);
            CHECK(w.world().colony_ship(ship)->goods_kg.at("aluminum_panels")==950);
            CHECK(w.world().colony_ship(ship)->goods_kg.at("aluminum_scrap")==50);
            CHECK(w.world().total_material_kg()==original_mass);
            w.steps(5);
            CHECK(w.world().workshop(site)->completed_batches>=53);
            const auto snap=w.world().canonical_state();
            CHECK(snap.rfind("galaxy-kernel-v22|",0)==0);
            CHECK(Simulation::from_snapshot(snap).canonical_state()==snap);
            // A corrupted installed machine must fail snapshot validation.
            auto corrupt=snap;
            const auto row_prefix="workshop|"+std::to_string(site)+"|"+
                std::to_string(ship)+"|2|";
            const auto where=corrupt.find(row_prefix);
            CHECK(where!=std::string::npos);
            corrupt.replace(where,row_prefix.size(),"workshop|"+std::to_string(site)+
                "|"+std::to_string(ship)+"|3|");
            CHECK(malformed_snapshot_rejected(corrupt));
            // Inventing a partial, below-minimum WIP batch is also invalid.
            corrupt=snap;
            const auto wpos=corrupt.find("workshop|");
            CHECK(wpos!=std::string::npos);
            auto eol=corrupt.find('\n',wpos);
            CHECK(eol!=std::string::npos);
            std::string row=corrupt.substr(wpos,eol-wpos);
            std::size_t start=0,field=0;
            while(field<7){start=row.find('|',start);CHECK(start!=std::string::npos);++start;++field;}
            const auto end_field=row.find('|',start);
            CHECK(end_field!=std::string::npos);
            row.replace(start,end_field-start,"19");
            corrupt.replace(wpos,eol-wpos,row);
            CHECK(malformed_snapshot_rejected(corrupt));
            PersistentWorld::recover_copy(db,(dir/"final.sqlite").string());
        }
        {
            PersistentWorld w(db,Simulation(1));
            CHECK(w.world().workshop(site)->completed_batches>=53);
            CHECK(w.world().total_material_kg()==original_mass);
        }
        {
            PersistentWorld w((dir/"final.sqlite").string(),Simulation(1));
            CHECK(w.world().workshop(site)->completed_batches>=53);
            CHECK(w.world().total_material_kg()==original_mass);
        }
        fs::remove_all(dir);
        std::cout<<"ECON-02B1a real installed outpost, finite input, NPC, power, WIP, storage, cargo, recovery: PASS\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"ECON-02B1a FAIL: "<<e.what()<<'\n';
        fs::remove_all(dir);return 1;
    }
}
