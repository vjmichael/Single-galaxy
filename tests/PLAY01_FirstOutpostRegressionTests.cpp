#include "galaxy/Persistence.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do { if(!(x))throw std::runtime_error(std::string("PLAY01 line ")+std::to_string(__LINE__)+": " #x); } while(false)
static Command order(PersistentWorld& w,std::uint64_t id,EntityId ship,Action action,
                     std::int64_t amount=1,EntityId target=0,PlayerId owner=7){
    Command c{};c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
    c.issuer=owner;c.actor=ship;c.target=target;c.action=action;c.amount=amount;
    return c;
}
static void execute(PersistentWorld& w,const Command& c){
    CHECK(w.submit(c));w.step();
    CHECK(w.world().events().back().command_id==c.id);
    CHECK(w.world().events().back().result==Outcome::Applied);
}
int main(){
    const auto folder=fs::temp_directory_path()/
        ("sg_play01_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try{
        fs::create_directories(folder);
        const auto db=(folder/"world.sqlite").string();
        const auto recovered=(folder/"recovered.sqlite").string();
        EntityId ship=0,rock=0,outpost=0;
        std::uint64_t hash_at_checkpoint=0;
        {
            Simulation seed(9001);
            ship=seed.add_colony_ship(7,{0,0},60,24);
            rock=seed.add_asteroid({1800,0},40);
            PersistentWorld w(db,std::move(seed));
            CHECK(w.world().colony_ship(ship)->crew==24);
            CHECK(w.world().built_outposts().empty());
            CHECK(w.world().total_ore()==40);
            CHECK(w.world().find(ship)->cargo==0);
            // First player action: no remote production, no free resources.
            execute(w,order(w,101,ship,Action::Mine,16,rock));
            CHECK(w.world().find(ship)->cargo==16);
            CHECK(w.world().find(rock)->ore==24);
            CHECK(w.world().total_ore()==40);
            hash_at_checkpoint=w.world().state_hash();
        }
        {
            PersistentWorld w(db,Simulation(9002));
            CHECK(w.world().state_hash()==hash_at_checkpoint);
            CHECK(w.world().find(ship)->cargo==16);
            execute(w,order(w,102,ship,Action::RefineOre,16));
            CHECK(w.world().colony_ship(ship)->refined==16);
            execute(w,order(w,103,ship,Action::FabricateModule,4));
            CHECK(w.world().colony_ship(ship)->modules==4);
            CHECK(w.world().colony_ship(ship)->refined==8);
            CHECK(w.world().total_ore()==40);
        }
        {
            PersistentWorld w(db,Simulation(9003));
            CHECK(w.world().colony_ship(ship)->modules==4);
            execute(w,order(w,104,ship,Action::ConstructOutpost));
            CHECK(w.world().built_outposts().size()==1);
            outpost=w.world().built_outposts().begin()->first;
            CHECK(w.world().find(outpost)->owner_id==7);
            CHECK(w.world().colony_ship(ship)->modules==0);
            CHECK(w.world().colony_ship(ship)->crew==24);
            CHECK(w.world().total_ore()==40);
            // Command replay across stages must never grant a second outpost.
            CHECK(!w.submit(order(w,104,ship,Action::ConstructOutpost)));
            CHECK(w.world().built_outposts().size()==1);
            PersistentWorld::recover_copy(db,recovered);
        }
        {
            PersistentWorld w(recovered,Simulation(9004));
            CHECK(w.world().built_outposts().size()==1);
            CHECK(w.world().built_outposts().count(outpost)==1);
            CHECK(w.world().colony_ship(ship)->crew==24);
            CHECK(w.world().find(rock)->ore==24);
            CHECK(w.world().total_ore()==40);
            CHECK(w.world().find(ship)->cargo==0);
            CHECK(w.world().colony_ship(ship)->refined==8);
        }
        fs::remove_all(folder);
        std::cout<<"PASS PLAY01 opening progression: finite crew, real mined materials, production, first outpost, save/restart at each stage, recovered copy, no duplicate construction\\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL PLAY01: "<<e.what()<<'\\n';
        std::error_code ignored;fs::remove_all(folder,ignored);return 1;
    }
}
