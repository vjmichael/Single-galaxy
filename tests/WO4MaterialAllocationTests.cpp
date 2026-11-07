#include "galaxy/WO4MaterialAllocation.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error( \
    "WO4 material line "+std::to_string(__LINE__)+": " #x);}while(0)
static void applied(PersistentWorld& world,EntityId ship,
    std::uint64_t id,Action act,EntityId dest=0,std::int64_t amount=1){
    Command command{};
    command.id=id;command.sequence=id;command.issuer=7;command.actor=ship;
    command.target=dest;command.action=act;command.amount=amount;
    command.execute_tick=world.world().tick()+1;
    CHECK(world.submit(command));
    world.step();
    CHECK(world.world().events().back().command_id==id &&
          world.world().events().back().result==Outcome::Applied);
}
int main() {
 try {
    using namespace galaxy::economy04;
    const auto folder=fs::temp_directory_path()/
        ("wo4_material_"+std::to_string(std::chrono::steady_clock::now().
                                      time_since_epoch().count()));
    fs::create_directories(folder);
    struct Cleanup {fs::path path;~Cleanup(){
        std::error_code ec;fs::remove_all(path,ec);
    }} cleanup{folder};
    const auto db=(folder/"physical.sqlite").string();
    Simulation genesis(98024);
    const auto ship=genesis.add_colony_ship(7,{0,0},300,8);
    genesis.provision_colony_genesis(ship,1000,3000,1000,1000,1000);
    const auto rock=genesis.add_asteroid({1000,0},100);
    const auto silicate=genesis.add_geological_asteroid(
        {1000,0},AsteroidClass::Silicate,30,2500);
    genesis.enable_individual_workforce();
    const auto starting_mass=genesis.total_material_kg();
    PersistentWorld world(db,std::move(genesis));
    applied(world,ship,1,Action::Mine,rock,80);
    applied(world,ship,2,Action::RefineOre,0,80);
    applied(world,ship,3,Action::FabricateModule,0,40);
    applied(world,ship,4,Action::ConstructOutpost);
    const auto site=world.world().built_outposts().begin()->first;
    applied(world,ship,5,Action::ConstructStorage,site);
    CHECK(world.world().storage(site));
    applied(world,ship,6,Action::Mine,silicate,45);
    applied(world,ship,7,Action::RefineMineral,10,18000);
    const auto before_mass=world.world().total_material_kg();
    CHECK(before_mass==starting_mass);
    const auto before_ship=world.world().ship_inventory_snapshot(ship).
        amount("good/aluminum");
    CHECK(before_ship>=200);
    CHECK(world.world().storage_inventory_snapshot(site).
        amount("good/aluminum")==0);
    CHECK(!propose_original_storage_allocation(
        world.world(),8,ship,site,NativeMaterial::Aluminum,
        Priority::Production,100,100));
    CHECK(!propose_original_storage_allocation(
        world.world(),7,ship,site,static_cast<NativeMaterial>(42),
        Priority::Production,100,100));
    CHECK(!propose_original_storage_allocation(
        world.world(),7,ship,site,NativeMaterial::Aluminum,
        Priority::VitalReserve,100,100));
    CHECK(!propose_original_storage_allocation(
        world.world(),7,ship,site,NativeMaterial::Aluminum,
        Priority::Production,100,before_ship));
    const auto offered=propose_original_storage_allocation(
        world.world(),7,ship,site,NativeMaterial::Aluminum,
        Priority::Production,100,100);
    CHECK(offered && offered->proposed_kg==100 &&
          offered->observed_storage_kg==0 &&
          offered->observed_ship_kg==before_ship);
    const auto unmodified=world.world().state_hash();
    CHECK(commit_owner_approved_storage_allocation(
        world,*offered,8,false).status==CommitStatus::Refused);
    auto forged=*offered;forged.proposed_kg=101;
    CHECK(commit_owner_approved_storage_allocation(
        world,forged,8,true).status==CommitStatus::Refused);
    CHECK(world.world().state_hash()==unmodified);
    // The trusted original command checks proximity, ownership, exact
    // storage capacity and journal receipts. It is not a WO4 shadow move.
    const auto done=commit_owner_approved_storage_allocation(
        world,*offered,8,true);
    CHECK(done.status==CommitStatus::Applied &&
          done.native_outcome==Outcome::Applied &&
          done.native_transferred_kg==100);
    CHECK(world.world().has_command_id(8));
    CHECK(world.world().ship_inventory_snapshot(ship).
        amount("good/aluminum")==before_ship-100);
    CHECK(world.world().storage_inventory_snapshot(site).
        amount("good/aluminum")==100);
    CHECK(world.world().total_material_kg()==starting_mass);
    CHECK(commit_owner_approved_storage_allocation(
        world,*offered,8,true).status==CommitStatus::Refused);
    CHECK(!propose_original_storage_allocation(
        world.world(),7,ship,site,NativeMaterial::Aluminum,
        Priority::Production,100,before_ship-100));
    // Independent SQLite copied world sees only one actual 100kg delivery.
    const auto duplicate=(folder/"recovered.sqlite").string();
    PersistentWorld::recover_copy(db,duplicate);
    PersistentWorld recovered(duplicate,Simulation(98025));
    CHECK(recovered.world().state_hash()==world.world().state_hash());
    CHECK(recovered.world().has_command_id(8));
    CHECK(recovered.world().storage_inventory_snapshot(site).
        amount("good/aluminum")==100);
    CHECK(recovered.world().total_material_kg()==starting_mass);
    std::cout<<"WO4-C material stockpile: original command, owner approval,"
                " material conservation and SQLite copy: OK\n";
    return 0;
 }catch(const std::exception& e) {
    std::cerr<<"WO4-C material test failed: "<<e.what()<<'\n';
    return 1;
 }
}
