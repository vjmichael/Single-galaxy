#include "galaxy/Persistence.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(#x)+" line "+std::to_string(__LINE__));}while(false)

static Outcome fire(PersistentWorld& world,PlayerId owner,
                    EntityId attacker,EntityId victim,std::uint64_t id){
    Command c{};
    c.id=id;c.sequence=id;c.execute_tick=world.world().tick()+1;
    c.issuer=owner;c.actor=attacker;c.target=victim;
    c.action=Action::FireShipWeapon;
    CHECK(world.submit(c));
    world.step();
    CHECK(!world.world().events().empty());
    const auto& event=world.world().events().back();
    CHECK(event.command_id==id);
    return event.result;
}
int main(){
 const auto dir=fs::temp_directory_path()/
    ("sg_combat01a_"+std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
 try{
    fs::create_directories(dir);
    const auto db=(dir/"world.sqlite").string();
    const auto copy=(dir/"copy.sqlite").string();
    EntityId home=0,raider=0,second=0;
    std::string canonical;
    {
        Simulation genesis(8808);
        home=genesis.add_colony_ship(7,{0,0},100,6);
        raider=genesis.add_ship(8,{700,0},100);
        second=genesis.add_ship(8,{-500,0},100);
        genesis.provision_colony_genesis(
            home,2000,2000,2000,2000,2000);
        genesis.enable_individual_workforce();
        genesis.upgrade_assignment_snapshot_format();
        genesis.enable_passenger_life_support();
        PersistentWorld world(db,std::move(genesis));
        CHECK(world.world().find(home)->hull==100);
        CHECK(world.world().find(raider)->hull==100);
        CHECK(world.world().people_physically_aboard(home)>0);
        CHECK(fire(world,8,raider,home,7001)==Outcome::OutOfRange);
        CHECK(world.world().find(home)->hull==100); // +X aim cannot fire backward
        CHECK(fire(world,7,home,home,7002)==Outcome::InvalidTarget ||
              world.world().find(home)->hull==100);
        // Finite original 12-charge ammo; real yaw/range, no renderer trust.
        std::uint64_t id=7100;
        for(int shot=0;shot<7;++shot){
            if(shot)world.step();
            CHECK(fire(world,7,home,raider,++id)==Outcome::Applied);
        }
        CHECK(world.world().find(raider)->type==EntityType::Wreck);
        CHECK(world.world().find(raider)->hull==0);
        CHECK(world.world().find(raider)->owner_id==8);
        CHECK(world.world().find(raider)->weapon_charges==0);
        CHECK(world.world().find(home)->weapon_charges==5);
        CHECK(fire(world,7,home,raider,7200)==Outcome::InvalidTarget);
        // Physically crewed/settled colony ships cannot be exterminated by
        // the first-pass weapon action before human custody/rescue is coded.
        for(int shot=0;shot<7;++shot){
            if(shot)world.step();
            CHECK(fire(world,8,second,home,7300+shot)==Outcome::Applied);
        }
        CHECK(world.world().find(home)->type==EntityType::Ship);
        CHECK(world.world().find(home)->hull==1);
        CHECK(world.world().people_physically_aboard(home)>0);
        CHECK(fire(world,8,second,home,7400)==Outcome::AttackResisted);
        CHECK(world.world().find(second)->weapon_charges==5);
        CHECK(world.world().colony_ship(home)!=nullptr);
        CHECK(world.world().people_physically_aboard(home)>0);
        CHECK(world.world().canonical_state().find("galaxy-kernel-v104|")!=
              std::string::npos);
        canonical=world.world().canonical_state();
        PersistentWorld::recover_copy(db,copy);
    }
    {
        PersistentWorld saved(copy,Simulation(2));
        CHECK(saved.world().canonical_state()==canonical);
        CHECK(saved.world().find(home)->hull==1);
        CHECK(saved.world().find(raider)->type==EntityType::Wreck);
        CHECK(saved.world().find(home)->weapon_charges==5);
        CHECK(saved.world().find(second)->weapon_charges==5);
        CHECK(saved.world().people_physically_aboard(home)>0);
    }
    fs::remove_all(dir);
    std::cout<<"PASS COMBAT-01A: actual finite-ammo native ship hull damage, physical uncrewed wreck, protected surviving colony crew and v104 restart\n";
    return 0;
 }catch(const std::exception& e){
    std::cerr<<"FAIL COMBAT-01A "<<e.what()<<'\n';
    std::error_code ignored;fs::remove_all(dir,ignored);
    return 1;
 }
}
