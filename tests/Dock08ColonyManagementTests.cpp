// DOCK-08 development-stage original-world evidence. Exact-head compiler,
// native, installed app and physical operator acceptance are still deferred.
// No second NPC roster, cloned factory balance or free material fixture.
#include "galaxy/ColonyIndustrialManagement.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
namespace fs=std::filesystem;
using namespace galaxy;
#define CHECK(x) do{if(!(x))throw std::runtime_error("DOCK08 line "+std::to_string(__LINE__)+": " #x);}while(0)
static Event issue(PersistentWorld& w,std::uint64_t& sequence,
                   EntityId actor,Action action,EntityId target=0,
                   std::int64_t quantity=1,int option=0) {
    Command c{};
    c.id=++sequence;c.sequence=c.id;c.execute_tick=w.world().tick()+1;
    c.issuer=7;c.actor=actor;c.action=action;c.target=target;
    c.amount=quantity;c.thrust_x=option;
    CHECK(w.submit(c));
    w.step();
    const auto event=w.world().events().back();
    CHECK(event.command_id==c.id);
    return event;
}
static void applied(PersistentWorld& w,std::uint64_t& sequence,EntityId actor,
                    Action action,EntityId target=0,
                    std::int64_t quantity=1,int option=0) {
    CHECK(issue(w,sequence,actor,action,target,quantity,option).result==Outcome::Applied);
}
static void erase(const std::string& path) {
    for(const auto* ext:{"","-wal","-shm"}){
        std::error_code ec;fs::remove(path+ext,ec);
    }
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    const std::string db=argv[1];
    erase(db);
    try{
        EntityId ship=0,site=0,other=0;
        std::uint64_t seq=81000;
        std::string stable;
        std::int64_t preserved_mass=0;
        {
            Simulation genesis(0xD008);
            ship=genesis.add_colony_ship(7,{0,0},250,8);
            genesis.provision_colony_genesis(ship,1000,3000,1000,1000,1000);
            other=genesis.add_station({75000,0});
            const auto rock=genesis.add_asteroid({2000,0},150);
            const auto geology=genesis.add_geological_asteroid(
                {1000,0},AsteroidClass::Silicate,30,2500);
            genesis.enable_individual_workforce();
            preserved_mass=genesis.total_material_kg();
            PersistentWorld w(db,std::move(genesis));
            applied(w,seq,ship,Action::Mine,rock,90);
            applied(w,seq,ship,Action::RefineOre,0,90);
            applied(w,seq,ship,Action::FabricateModule,0,30);
            applied(w,seq,ship,Action::ConstructOutpost);
            site=w.world().built_outposts().begin()->first;
            CHECK(site!=other);
            // All physical facilities consume original construction modules.
            applied(w,seq,ship,Action::ConstructWorkshop,site);
            applied(w,seq,ship,Action::ConstructStorage,site);
            applied(w,seq,ship,Action::ConstructUtility,site);
            applied(w,seq,ship,Action::ConstructHabitat,site);
            for(int index=1;index<=3;++index)
                applied(w,seq,ship,Action::TransferHabitatStock,site,100,index);
            applied(w,seq,ship,Action::AssignHabitatResidents,site,3);
            applied(w,seq,ship,Action::AssignUtilityTechnicians,site,1);
            applied(w,seq,ship,Action::TransferUtilitySpares,site,5);
            CHECK(w.world().utility(site));
            // Original finite geological processing, not station
            // concentrate transmutation or synthetic panel feed.
            applied(w,seq,ship,Action::Mine,geology,30);
            applied(w,seq,ship,Action::RefineMineral,10,12000);
            applied(w,seq,ship,Action::TransferWorkshopFeed,site,1000);
            w.steps(12);
            CHECK(w.world().total_material_kg()==preserved_mass);
            applied(w,seq,ship,Action::RequestDock,site);
            w.steps(Simulation::kDockTransitionTicks);
            CHECK(w.world().find(ship)->dock_state==DockState::Docked);
            CHECK(w.register_player_at_station(7,site));
            CHECK(colony_industry::local_resident(w.world(),7,site));
            CHECK(!colony_industry::local_resident(w.world(),7,other));
            CHECK(!colony_industry::local_resident(w.world(),8,site));
            CHECK(!colony_management::propose(w.world(),8,site,13,seq+1));
            CHECK(!colony_management::propose(w.world(),7,other,13,seq+1));
            CHECK(!colony_management::propose(w.world(),7,site,0,seq+1));
            CHECK(!colony_management::propose(w.world(),7,site,29,seq+1));
            // Already-built original facilities cannot be built again.
            CHECK(!colony_management::propose(w.world(),7,site,1,seq+1));
            CHECK(!colony_management::propose(w.world(),7,site,2,seq+1));
            CHECK(!colony_management::propose(w.world(),7,site,3,seq+1));
            CHECK(!colony_management::propose(w.world(),7,site,4,seq+1));
            CHECK(colony_management::propose(w.world(),7,site,13,seq+1));
            const auto material_before=w.world().total_material_kg();
            const auto hash_before=w.world().state_hash();
            const auto read=colony_management::make_snapshot(w,7,91,12);
            CHECK(read.find("IMHEAD|1|91|12|")!=std::string::npos);
            CHECK(read.find("IMWORK|1|91|12|1|")!=std::string::npos);
            CHECK(read.find("IMOPT|1|91|12|13|1")!=std::string::npos);
            CHECK(read.find("IMEND|1|91|12|")!=std::string::npos);
            CHECK(w.world().state_hash()==hash_before);
            CHECK(w.world().total_material_kg()==material_before);
            // Every manager command is human-selected and run by the
            // original world. No local-only event credits.
            const auto maintenance=colony_management::propose(
                w.world(),7,site,13,++seq);
            CHECK(maintenance);
            CHECK(w.submit(*maintenance));
            w.step();
            CHECK(w.world().events().back().command_id==seq);
            CHECK(w.world().events().back().result==Outcome::Applied);
            CHECK(w.world().utility(site)->delegated_maintenance);
            CHECK(!colony_management::propose(w.world(),7,site,13,seq+1));
            // Training: one real kg/energy/worker per original receipt, not
            // a fabricated qualification. A previously uncertified named
            // NPC can only become a supervisor after three credited acts.
            const auto plan=workforce02::plan_original_workshop(
                w.world(),7,ship,site);
            CHECK(plan[0].suggested_npc);
            const auto trainee=plan[0].suggested_npc;
            CHECK(!w.world().workshop_trade_certified(trainee,WorkshopTrade::Cut));
            for(int n=0;n<3;++n){
                const auto training=colony_management::propose(
                    w.world(),7,site,15,++seq);
                CHECK(training && training->action==Action::TrainWorkshopTrade);
                CHECK(training->amount==static_cast<std::int64_t>(trainee));
                CHECK(w.submit(*training));w.step();
                CHECK(w.world().events().back().command_id==seq);
                CHECK(w.world().events().back().result==Outcome::Applied);
                CHECK(w.world().total_material_kg()==material_before);
            }
            CHECK(w.world().workshop_trade_certified(trainee,WorkshopTrade::Cut));
            const auto supervisor=colony_management::propose(
                w.world(),7,site,17,++seq);
            CHECK(supervisor && supervisor->amount==static_cast<std::int64_t>(trainee));
            CHECK(w.submit(*supervisor));w.step();
            CHECK(w.world().events().back().command_id==seq);
            CHECK(w.world().events().back().result==Outcome::Applied);
            CHECK(w.world().workshop(site)->supervisor_npc_id==trainee);
            const auto staffing=colony_management::propose(
                w.world(),7,site,19,seq+1);
            CHECK(staffing && staffing->action==Action::StartWorkshopShift);
            CHECK(staffing->amount>0 &&
                  staffing->amount!=static_cast<std::int64_t>(trainee));
            CHECK(staffing->thrust_y==16);
            CHECK(!colony_management::propose(w.world(),7,site,17,seq+1));
            // Conservatively stage shift proposal but do not infer that
            // scheduling alone delivered any fabricated finished goods.
            CHECK(w.world().total_material_kg()==preserved_mass);
            stable=colony_management::make_snapshot(w,7,91,13);
        }
        {
            PersistentWorld reopened(db,Simulation(0xD009));
            CHECK(reopened.world().player_people().at(7).physical_station==site);
            CHECK(reopened.world().workshop(site)->supervisor_npc_id>0);
            CHECK(reopened.world().utility(site)->delegated_maintenance);
            CHECK(reopened.world().total_material_kg()==preserved_mass);
            CHECK(colony_management::make_snapshot(reopened,7,91,13)==stable);
            CHECK(!colony_industry::local_resident(reopened.world(),7,other));
        }
        erase(db);
        std::cout<<"DOCK-08 staged physical facility/workforce/restart PASS\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"DOCK-08 staged native source failure: "<<e.what()<<'\n';
        erase(db);
        return 1;
    }
}
