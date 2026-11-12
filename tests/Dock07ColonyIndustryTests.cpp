// DOCK-07 staged native regression: colony-local inventory is a projection of
// original saved world state, never a second account-global store.
#include "galaxy/ColonyIndustrialService.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
namespace fs=std::filesystem;
using namespace galaxy;
#define CHECK(x) do{if(!(x))throw std::runtime_error("DOCK07 line "+std::to_string(__LINE__)+": " #x);}while(0)
static Event applied(PersistentWorld& w,std::uint64_t& seq,EntityId actor,
                     Action action,EntityId target=0,std::int64_t quantity=1,
                     int item=0,int direction=0){
    Command c{};c.id=++seq;c.sequence=c.id;c.execute_tick=w.world().tick()+1;
    c.issuer=7;c.actor=actor;c.action=action;c.target=target;c.amount=quantity;
    c.thrust_x=item;c.thrust_y=direction;
    CHECK(w.submit(c));
    w.step();
    const auto event=w.world().events().back();
    CHECK(event.command_id==c.id && event.result==Outcome::Applied);
    return event;
}
static void erase(const std::string& path){
    for(const auto* suffix:{"","-wal","-shm"}){
        std::error_code ec;fs::remove(path+suffix,ec);
    }
}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    const std::string file=argv[1];
    erase(file);
    try{
        EntityId local=0,ship=0,other=0;
        std::uint64_t seq=74000;
        std::string saved;
        {
            Simulation s(0xD007);
            ship=s.add_colony_ship(7,{0,0},200,8);
            other=s.add_station({75000,0});
            s.provision_colony_genesis(ship,1000,3000,1000,1000,1000);
            const auto raw=s.add_asteroid({2000,0},150);
            const auto silicate=s.add_geological_asteroid(
                {1000,0},AsteroidClass::Silicate,30,2500);
            s.enable_individual_workforce();
            PersistentWorld w(file,std::move(s));
            applied(w,seq,ship,Action::Mine,raw,80);
            applied(w,seq,ship,Action::RefineOre,0,80);
            applied(w,seq,ship,Action::FabricateModule,0,20);
            applied(w,seq,ship,Action::ConstructOutpost);
            local=w.world().built_outposts().begin()->first;
            CHECK(local!=other);
            applied(w,seq,ship,Action::ConstructWorkshop,local);
            applied(w,seq,ship,Action::ConstructStorage,local);
            // Real original geology and processing create the finite
            // aluminum feed; the native station panel never supplies it.
            applied(w,seq,ship,Action::Mine,silicate,45);
            applied(w,seq,ship,Action::RefineMineral,10,18000);
            CHECK(w.world().ship_inventory_snapshot(ship)
                .amount("good/aluminum")>=20);
            applied(w,seq,ship,Action::RequestDock,local);
            w.steps(Simulation::kDockTransitionTicks);
            CHECK(w.world().find(ship)->dock_state==DockState::Docked);
            CHECK(w.register_player_at_station(7,local));
            CHECK(colony_industry::local_resident(w.world(),7,local));
            CHECK(!colony_industry::local_resident(w.world(),7,other));
            CHECK(colony_industry::local_source_ship(w.world(),local,7)==ship);
            CHECK(!colony_industry::local_source_ship(w.world(),other,7));
            CHECK(!colony_industry::local_source_ship(w.world(),local,8));
            CHECK(colony_industry::native_workshop_at(w.world(),local,7));
            CHECK(colony_industry::native_storage_at(w.world(),local,7));
            const auto physical_mass=w.world().total_material_kg();
            const auto ship_before=w.world().ship_inventory_snapshot(ship)
                .amount("good/aluminum");
            // No Godot stock edit: source original ship -> constructed
            // warehouse -> original workshop, 20 kg each with exact receipts.
            applied(w,seq,ship,Action::TransferStorageStack,local,20,0,0);
            CHECK(w.world().storage_inventory_snapshot(local)
                .amount("good/aluminum")==20);
            CHECK(w.world().ship_inventory_snapshot(ship)
                .amount("good/aluminum")==ship_before-20);
            applied(w,seq,ship,Action::TransferStorageStack,local,20,0,3);
            CHECK(w.world().storage_inventory_snapshot(local)
                .amount("good/aluminum")==0);
            CHECK(w.world().workshop(local)->aluminum_kg==20);
            CHECK(w.world().total_material_kg()==physical_mass);
            CHECK(!colony_industry::native_storage_at(w.world(),other,7));
            const auto before=w.world().state_hash();
            const auto before_mass=w.world().total_material_kg();
            saved=colony_industry::make_snapshot(w,7,11,1);
            CHECK(saved.find("IHEAD|1|11|1|")!=std::string::npos);
            CHECK(saved.find("IJOB|1|11|1|1|0|")!=std::string::npos);
            CHECK(saved.find("IJOB|1|11|1|2|0|")!=std::string::npos);
            CHECK(saved.find("IEND|1|11|1|")!=std::string::npos);
            CHECK(saved.find("|H|0|good/aluminum|")!=std::string::npos);
            CHECK(saved.find("|W|0|good/aluminum|20")!=std::string::npos);
            CHECK(saved.find("|"+std::to_string(local)+"|1|")!=std::string::npos);
            CHECK(w.world().state_hash()==before);
            CHECK(w.world().total_material_kg()==before_mass);
            bool refused=false;
            try{(void)colony_industry::make_snapshot(w,8,11,1);}
            catch(const std::invalid_argument&){refused=true;}
            CHECK(refused);
            // No hidden supervisor or free inputs: neither native recipe
            // can receive a fabricated work order from the colony panel.
            CHECK(!workforce02::approved_workshop_job(w.world(),7,ship,local,
                  WorkshopTrade::Cut,1,seq+1,true));
            CHECK(!workforce02::approved_workshop_job(w.world(),7,ship,local,
                  WorkshopTrade::Shape,1,seq+2,true));
            CHECK(!colony_industry::safe_item_code("good|forged"));
            CHECK(!colony_industry::safe_item_code("../remote"));
            CHECK(colony_industry::safe_item_code("good/aluminum_panels"));
        }
        {
            PersistentWorld reopened(file,Simulation(0xD008));
            CHECK(reopened.world().player_people().at(7).physical_station==local);
            CHECK(colony_industry::native_workshop_at(reopened.world(),local,7));
            CHECK(colony_industry::native_storage_at(reopened.world(),local,7));
            const auto view=colony_industry::make_snapshot(reopened,7,11,1);
            CHECK(view==saved);
            CHECK(!colony_industry::local_resident(reopened.world(),7,other));
        }
        erase(file);
        std::cout<<"DOCK-07 staged native colony industry isolation/restart PASS\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"DOCK-07 staged source failure: "<<e.what()<<'\n';
        erase(file);
        return 1;
    }
}
