#include "galaxy/Industry01Catalog.hpp"
#include "galaxy/Persistence.hpp"
#include "galaxy/Inventory.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <functional>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do {if(!(x))throw std::runtime_error("INDUSTRY01 native line "+std::to_string(__LINE__)+": " #x);}while(0)

static void refuses(const std::function<void()>& fn){
    bool rejected=false;
    try{fn();}catch(const std::exception&){rejected=true;}
    CHECK(rejected);
}
static Event perform(PersistentWorld& w,EntityId ship,std::uint64_t id,
                     Action action,EntityId target=0,std::int64_t amount=1,int option=0){
    Command c{};c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
    c.issuer=7;c.actor=ship;c.target=target;c.action=action;
    c.amount=amount;c.thrust_x=option;
    CHECK(w.submit(c));
    w.step();
    return w.world().events().back();
}
static void applied(PersistentWorld& w,EntityId ship,std::uint64_t id,
                    Action action,EntityId target=0,std::int64_t amount=1,int opt=0){
    CHECK(perform(w,ship,id,action,target,amount,opt).result==Outcome::Applied);
}
int main(){
 try {
    industry01::validate_against_original_inventory();
    CHECK(industry01::items().size()==100);
    CHECK(industry01::recipes().size()==71);
    CHECK(industry01::services().size()==8);
    CHECK(industry01::legacy_units().size()==28);
    CHECK(industry01::find_item("bulk/aluminum")!=nullptr);
    CHECK(industry01::find_item("item/houseplant")!=nullptr);
    CHECK(industry01::find_service("service/laundry")!=nullptr);
    CHECK(industry01::find_recipe("assemble_video_camera")!=nullptr);
    CHECK(industry01::find_recipe("invented_recipe")==nullptr);
    CHECK(!industry01::is_native_workshop_recipe("assemble_video_camera"));
    CHECK(industry01::is_native_workshop_recipe("native_cut_aluminum_panel"));
    CHECK(industry01::is_native_workshop_recipe("native_shape_aluminum_frame"));
    refuses([]{(void)industry01::original_workshop_recipe("assemble_video_camera");});
    const auto& volume=inventory::Catalogue::standard();
    CHECK(volume.liters_per_unit("ore")==1000);
    CHECK(volume.liters_per_unit("good/aluminum")==1);
    CHECK(volume.liters_per_unit("good/freight_nutrients")==1);

    // Actual ORIGINAL QuantifiedLine worker/power/maintenance/backpressure
    // constraints, not a second item or worker record.
    auto native=industry01::original_workshop_recipe("native_cut_aluminum_panel");
    CHECK(native.name=="aluminum_panel" && native.operation=="cut");
    CHECK(native.inputs.size()==1 && native.inputs[0].grams==20000);
    CHECK(native.product.code=="aluminum_panels" && native.product.grams==19000);
    CHECK(native.scrap.code=="aluminum_scrap" && native.scrap.grams==1000);
    industry::State state{};
    state.feed.capacity_grams=100000;
    state.finished.capacity_grams=100000;
    state.scrap.capacity_grams=100000;
    state.feed.deposit("aluminum",20000);
    industry::QuantifiedLine line(native,{1,"cut",true,true,true},state);
    line.advance(2);
    CHECK(line.state().has_work_in_progress);
    CHECK(line.state().ticks_remaining==3);
    CHECK(line.state().finished.used()==0);
    CHECK(line.total_mass_grams()==20000);
    industry::QuantifiedLine recovered_line(native,{1,"cut",false,true,true},line.state());
    recovered_line.advance(200);
    CHECK(recovered_line.state().ticks_remaining==3);
    recovered_line.set_power(true);
    recovered_line.advance(3);
    CHECK(recovered_line.state().completed_batches==1);
    CHECK(recovered_line.state().finished.amount("aluminum_panels")==19000);
    CHECK(recovered_line.state().scrap.amount("aluminum_scrap")==1000);
    CHECK(recovered_line.total_mass_grams()==20000);

    // Physical world loop: mine finite mineral, refine real aluminum,
    // construct outpost+powered workshop, assign EXISTING on-site NPC,
    // commit material-backed WIP and physically finished stock to SQLite.
    const auto folder=fs::temp_directory_path()/
        ("industry01_native_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(folder);
    const auto db=(folder/"original.sqlite").string();
    const auto copy=(folder/"independent.sqlite").string();
    EntityId ship=0,site=0;
    std::uint64_t npc=0;
    std::int64_t original_mass=0;
    {
        Simulation s(92001);
        ship=s.add_colony_ship(7,{0,0},200,8);
        s.provision_colony_genesis(ship,1000,3000,1000,1000,1000);
        const auto bulk=s.add_asteroid({2000,0},150);
        const auto silicate=s.add_geological_asteroid({1000,0},AsteroidClass::Silicate,30,2500);
        s.enable_individual_workforce();
        original_mass=s.total_material_kg();
        PersistentWorld w(db,std::move(s));
        applied(w,ship,1,Action::Mine,bulk,80);
        applied(w,ship,2,Action::RefineOre,0,80);
        applied(w,ship,3,Action::FabricateModule,0,20);
        applied(w,ship,4,Action::ConstructOutpost);
        site=w.world().built_outposts().begin()->first;
        applied(w,ship,6,Action::ConstructWorkshop,site);
        applied(w,ship,8,Action::ConstructUtility,site);
        applied(w,ship,9,Action::ConstructHabitat,site);
        for(int n=1;n<=3;++n)applied(w,ship,9+n,Action::TransferHabitatStock,site,100,n);
        applied(w,ship,13,Action::AssignHabitatResidents,site,2);
        applied(w,ship,14,Action::AssignUtilityTechnicians,site,1);
        applied(w,ship,15,Action::TransferUtilitySpares,site,5);
        applied(w,ship,16,Action::SetUtilityMaintenance,site,1);
        for(const auto& [id,n]:w.world().npc_records()){
            if(n.location==site && n.role==NpcRole::Habitat){npc=id;break;}
        }
        CHECK(npc!=0);
        applied(w,ship,19,Action::Mine,silicate,30);
        applied(w,ship,20,Action::RefineMineral,10,12000);
        CHECK(w.world().colony_ship(ship)->goods_kg.at("aluminum")==2400);
        applied(w,ship,21,Action::TransferWorkshopFeed,site,1000);
        CHECK(w.world().workshop(site)->aluminum_kg==1000);
        CHECK(w.world().total_material_kg()==original_mass);
        // Exactly one named NPC is assigned to existing physical machine.
        applied(w,ship,22,Action::AssignWorkshopOperator,site,
                static_cast<std::int64_t>(npc));
        CHECK(w.world().workshop(site)->operator_npc_id==npc);
        CHECK(w.world().workshop(site)->reserved_aluminum_kg==20);
        CHECK(w.world().workshop(site)->ticks_remaining==4);
        CHECK(w.world().total_material_kg()==original_mass);
        PersistentWorld::recover_copy(db,copy);
        {
            PersistentWorld independently_recovered(copy,Simulation(999));
            CHECK(independently_recovered.world().workshop(site)->operator_npc_id==npc);
            CHECK(independently_recovered.world().workshop(site)->reserved_aluminum_kg==20);
            independently_recovered.steps(4);
            CHECK(independently_recovered.world().workshop(site)->panels_kg==19);
            CHECK(independently_recovered.world().workshop(site)->scrap_kg==1);
            CHECK(independently_recovered.world().total_material_kg()==original_mass);
        }
        w.steps(4);
        const auto* workshop=w.world().workshop(site);
        CHECK(workshop->completed_batches==1);
        CHECK(workshop->aluminum_kg==980 && workshop->panels_kg==19);
        CHECK(workshop->scrap_kg==1 && workshop->reserved_aluminum_kg==0);
        CHECK(w.world().total_material_kg()==original_mass);
    }
    // Reopen real original SQLite world, not a separate candidate recipe database.
    {
        PersistentWorld opened(db,Simulation(123));
        CHECK(opened.world().workshop(site)!=nullptr);
        CHECK(opened.world().workshop(site)->operator_npc_id==npc);
        CHECK(opened.world().workshop(site)->completed_batches==1);
        CHECK(opened.world().workshop(site)->panels_kg==19);
        CHECK(opened.world().total_material_kg()==original_mass);
    }
    fs::remove_all(folder);
    std::cout<<"INDUSTRY01: registered source recipes, native worker-backed panels, conservation, original SQLite copy/restart: PASS\n";
    return 0;
 }catch(const std::exception& e){
    std::cerr<<"INDUSTRY01 native failure: "<<e.what()<<'\n';
    return 1;
 }
}
