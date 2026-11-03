#include "galaxy/Persistence.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do {if(!(x))throw std::runtime_error("ECON02B1b line "+std::to_string(__LINE__)+": " #x);}while(0)
static Event send(PersistentWorld& w,EntityId ship,std::uint64_t id,Action action,
                  EntityId target=0,std::int64_t amt=1,PlayerId issuer=7,int opt=0){
    Command c{};c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
    c.issuer=issuer;c.actor=ship;c.target=target;c.action=action;c.amount=amt;c.thrust_x=opt;
    CHECK(w.submit(c));w.step();return w.world().events().back();
}
static void ok(PersistentWorld& w,EntityId ship,std::uint64_t id,Action action,
               EntityId target=0,std::int64_t amt=1,int opt=0){
    auto ev=send(w,ship,id,action,target,amt,7,opt);
    CHECK(ev.result==Outcome::Applied);
}
static bool rejects(std::string v){
    try{(void)Simulation::from_snapshot(v);return false;}
    catch(const std::runtime_error&){return true;}
}
int main(){
    auto dir=fs::temp_directory_path()/("econ02b1b_"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    const auto db=(dir/"live.sqlite").string(),recovery=(dir/"recovered.sqlite").string();
    try{
        Simulation s(93001);
        EntityId ship=s.add_colony_ship(7,{0,0},200,8);
        s.provision_colony_genesis(ship,1000,3000,1000,1000,1000);
        const auto ore=s.add_asteroid({2000,0},150);
        const auto silicate=s.add_geological_asteroid({1000,0},AsteroidClass::Silicate,30,2500);
        s.enable_individual_workforce();
        const auto conserved=s.total_material_kg();
        EntityId site=0;std::uint64_t first=0,second=0;
        {
            PersistentWorld w(db,std::move(s));
            ok(w,ship,1,Action::Mine,ore,80);
            ok(w,ship,2,Action::RefineOre,0,80);
            ok(w,ship,3,Action::FabricateModule,0,40);
            ok(w,ship,4,Action::ConstructOutpost);
            site=w.world().built_outposts().begin()->first;
            ok(w,ship,5,Action::ConstructWorkshop,site);
            CHECK(send(w,ship,6,Action::ConstructWorkshopFinisher,site,1,8).result==Outcome::Unauthorized);
            CHECK(!w.world().workshop(site)->finisher_installed);
            ok(w,ship,7,Action::ConstructWorkshopFinisher,site);
            CHECK(w.world().workshop(site)->embodied_ore==4);
            CHECK(send(w,ship,8,Action::ConstructWorkshopFinisher,site).result==Outcome::InvalidTarget);
            CHECK(w.world().total_material_kg()==conserved);
            ok(w,ship,9,Action::ConstructUtility,site);
            ok(w,ship,10,Action::ConstructHabitat,site);
            for(int i=1;i<=3;++i)ok(w,ship,10+i,Action::TransferHabitatStock,site,500,i);
            ok(w,ship,14,Action::AssignHabitatResidents,site,2);
            ok(w,ship,15,Action::AssignUtilityTechnicians,site,1);
            ok(w,ship,16,Action::TransferUtilitySpares,site,25);
            ok(w,ship,17,Action::SetUtilityMaintenance,site,1);
            for(const auto& [id,n]:w.world().npc_records()){
                if(n.location==site && n.role==NpcRole::Habitat){
                    if(!first)first=id;else if(!second)second=id;
                }
            }
            CHECK(first && second && first!=second);
            CHECK(send(w,ship,18,Action::AssignWorkshopFinisher,site,99999).result==Outcome::InvalidTarget);
            ok(w,ship,19,Action::AssignWorkshopOperator,site,static_cast<std::int64_t>(first));
            CHECK(send(w,ship,20,Action::AssignWorkshopFinisher,site,static_cast<std::int64_t>(first)).result==Outcome::InvalidTarget);
            ok(w,ship,21,Action::AssignWorkshopFinisher,site,static_cast<std::int64_t>(second));
            CHECK(w.world().workshop(site)->finisher_batches==0);
            CHECK(send(w,ship,22,Action::TransferWorkshopFinalOutput,site,50).result==Outcome::NoResources);
            CHECK(w.world().workshop(site)->frames_kg==0);
            ok(w,ship,23,Action::Mine,silicate,45);
            ok(w,ship,24,Action::RefineMineral,10,18000);
            CHECK(w.world().colony_ship(ship)->goods_kg.at("aluminum")==3600);
            ok(w,ship,25,Action::TransferWorkshopFeed,site,1000);
            // A first processor batch yields 19kg, BELOW the finisher's 20kg
            // input minimum, so it must not start or produce finisher scrap.
            auto begin=*w.world().workshop(site);
            CHECK(begin.panels_kg==0 && begin.finisher_batches==0);
            w.steps(4);
            const auto one=*w.world().workshop(site);
            CHECK(one.panels_kg==19 && one.scrap_kg==1);
            CHECK(one.frames_kg==0 && one.finisher_batches==0 && one.reserved_panels_kg==0);
            CHECK(w.world().total_material_kg()==conserved);
            // The second batch creates enough intermediate material to reserve
            // a whole finisher batch; checkpoint it mid-cycle.
            w.steps(5);
            const auto mid=*w.world().workshop(site);
            CHECK(mid.reserved_panels_kg==20 && mid.finisher_ticks_remaining==4);
            CHECK(mid.frames_kg==0);
            const auto mid_snapshot=w.world().canonical_state();
            CHECK(mid_snapshot.rfind("galaxy-kernel-v31|",0)==0);
            CHECK(Simulation::from_snapshot(mid_snapshot).canonical_state()==mid_snapshot);
            PersistentWorld::recover_copy(db,recovery);
            {
                PersistentWorld recovered(recovery,Simulation(1));
                CHECK(recovered.world().workshop(site)->reserved_panels_kg==20);
                ok(recovered,ship,1001,Action::AssignWorkshopFinisher,site,0);
                const auto paused=*recovered.world().workshop(site);
                recovered.steps(8);
                CHECK(recovered.world().workshop(site)->reserved_panels_kg==20);
                CHECK(recovered.world().workshop(site)->finisher_ticks_remaining==paused.finisher_ticks_remaining);
                CHECK(recovered.world().workshop(site)->frames_kg==0);
                ok(recovered,ship,1002,Action::AssignWorkshopFinisher,site,static_cast<std::int64_t>(second));
                recovered.steps(3);
                CHECK(recovered.world().workshop(site)->frames_kg==19);
                CHECK(recovered.world().total_material_kg()==conserved);
            }
            w.steps(4);
            CHECK(w.world().workshop(site)->frames_kg==19);
            CHECK(w.world().total_material_kg()==conserved);
            // First tonne alone cannot supply 50 finisher batches because
            // upstream processing produced only 950kg of intermediate goods.
            w.steps(250);
            auto x=*w.world().workshop(site);
            CHECK(x.completed_batches==50 && x.finisher_batches==47);
            CHECK(x.aluminum_kg==0 && x.reserved_aluminum_kg==0);
            CHECK(x.panels_kg==10 && x.reserved_panels_kg==0);
            CHECK(x.frames_kg==893 && x.scrap_kg==97);
            CHECK(send(w,ship,26,Action::TransferWorkshopFinalOutput,site,50).result==Outcome::NoResources);
            const auto blocked=*w.world().workshop(site);
            w.steps(50);
            CHECK(w.world().workshop(site)->finisher_batches==blocked.finisher_batches);
            CHECK(w.world().workshop(site)->scrap_kg==blocked.scrap_kg);
            CHECK(w.world().total_material_kg()==conserved);
            // The next real 1-tonne shipment resumes both machines. A full
            // downstream bin eventually blocks the second machine, then fills
            // the bounded intermediate bin and blocks the first.
            ok(w,ship,27,Action::TransferWorkshopFeed,site,1000);
            w.steps(800);
            x=*w.world().workshop(site);
            CHECK(x.frames_kg==988); // 52 * 19kg, next 19kg would exceed 1t
            CHECK(x.finisher_batches==52);
            CHECK(x.panels_kg<=1000);
            CHECK(w.world().total_material_kg()==conserved);
            // Deliver another genuine 1t while downstream output is blocked.
            // The intermediate bin must then fill and block the upstream saw;
            // remaining aluminum must remain real and unconsumed.
            ok(w,ship,28,Action::TransferWorkshopFeed,site,1000);
            w.steps(300);
            x=*w.world().workshop(site);
            CHECK(x.finisher_batches==52 && x.frames_kg==988);
            CHECK(w.world().utility(site)->powered && w.world().utility(site)->condition>0);
            CHECK(x.panels_kg>=980 && x.panels_kg<=1000);
            CHECK(x.aluminum_kg>0);
            CHECK(x.reserved_aluminum_kg==0);
            const auto before=x;
            w.steps(40);
            x=*w.world().workshop(site);
            CHECK(x.finisher_batches==before.finisher_batches);
            CHECK(x.completed_batches==before.completed_batches);
            CHECK(x.frames_kg==before.frames_kg);
            CHECK(w.world().total_material_kg()==conserved);
            // 50 processed final batches = a conserved 1t pallet of
            // 950kg frames and 50kg scrap. Leftover upstream scrap stays local.
            ok(w,ship,29,Action::TransferWorkshopFinalOutput,site,50);
            CHECK(w.world().colony_ship(ship)->goods_kg.at("aluminum_frames")==950);
            CHECK(w.world().colony_ship(ship)->goods_kg.at("aluminum_scrap")==50);
            CHECK(w.world().workshop(site)->frames_kg==38);
            CHECK(w.world().total_material_kg()==conserved);
            const auto state=w.world().canonical_state();
            CHECK(Simulation::from_snapshot(state).canonical_state()==state);
            // Damaged WIP and counterfeit finisher body are rejected.
            std::string fake=state;
            auto pos=fake.find("workshop|");CHECK(pos!=std::string::npos);
            auto end=fake.find('\n',pos);CHECK(end!=std::string::npos);
            auto row=fake.substr(pos,end-pos);
            auto parts=std::vector<std::string>{};
            std::size_t start=0;
            while(true){auto sep=row.find('|',start);parts.push_back(row.substr(start,sep==std::string::npos?sep:sep-start));if(sep==std::string::npos)break;start=sep+1;}
            CHECK(parts.size()==19);
            auto f=parts;f[3]="6";
            std::string r;for(const auto& v:f){if(!r.empty())r+='|';r+=v;}
            fake.replace(pos,end-pos,r);CHECK(rejects(fake));
            f=parts;f[14]="19";
            r.clear();for(const auto& v:f){if(!r.empty())r+='|';r+=v;}
            fake=state;fake.replace(pos,end-pos,r);CHECK(rejects(fake));
            // Preserve machine records and mass through SQLite save and copy.
            PersistentWorld::recover_copy(db,(dir/"final.sqlite").string());
        }
        {
            PersistentWorld w(db,Simulation(1));
            CHECK(w.world().workshop(site)->finisher_installed);
            CHECK(w.world().workshop(site)->finisher_batches==52);
            CHECK(w.world().total_material_kg()==conserved);
        }
        {
            PersistentWorld w((dir/"final.sqlite").string(),Simulation(1));
            CHECK(w.world().workshop(site)->finisher_batches==52);
            CHECK(w.world().total_material_kg()==conserved);
        }
        // Resource provenance is checked independently against committed
        // action results. A counterfeit shipment destination is rejected.
        const auto forged=(dir/"forged.sqlite").string();
        fs::copy_file(db,forged);
        sqlite3* conn=nullptr;
        CHECK(sqlite3_open(forged.c_str(),&conn)==SQLITE_OK);
        char* error=nullptr;
        const int rc=sqlite3_exec(conn,
            "UPDATE resource_ledger SET destination_id='forged-output' "
            "WHERE reason='workshop_final_output_transfer_kg'",nullptr,nullptr,&error);
        if(error)sqlite3_free(error);
        CHECK(rc==SQLITE_OK);
        CHECK(sqlite3_changes(conn)==1);
        sqlite3_close(conn);
        bool tamper_rejected=false;
        try {PersistentWorld forged_world(forged,Simulation(1));}
        catch(const std::runtime_error&){tamper_rejected=true;}
        CHECK(tamper_rejected);
        fs::remove_all(dir);
        std::cout<<"ECON-02B1b two real machines, intermediate backpressure, labor, conservation, recovery: PASS\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"ECON-02B1b FAIL: "<<e.what()<<'\n';
        fs::remove_all(dir);return 1;
    }
}
