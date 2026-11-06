#include "galaxy/ClientContract.hpp"
#include "galaxy/Persistence.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(#x)+" at "+std::to_string(__LINE__));}while(false)
static void original_command(PersistentWorld& world,PlayerId owner,
    EntityId ship,EntityId target,Action action,std::int64_t amount,
    std::uint64_t id) {
    Command cmd{};cmd.id=id;cmd.sequence=id;
    cmd.execute_tick=world.world().tick()+1;cmd.issuer=owner;
    cmd.actor=ship;cmd.target=target;cmd.action=action;cmd.amount=amount;
    CHECK(world.submit(cmd));
    world.step();
    CHECK(world.world().events().back().command_id==id);
    CHECK(world.world().events().back().result==Outcome::Applied);
}
int main(){
 const auto dir=fs::temp_directory_path()/
    ("sg_status07_view_"+std::to_string(
     std::chrono::steady_clock::now().time_since_epoch().count()));
 try{
    fs::create_directories(dir);
    const auto db=(dir/"world.sqlite").string();
    Simulation seed(90707);
    const auto local=seed.add_colony_ship(7,{0,0},120,6);
    const auto foreign=seed.add_colony_ship(8,{2500,0},120,6);
    const auto asteroid=seed.add_asteroid({2650,0},12);
    for(const auto ship:{local,foreign})
        seed.provision_colony_genesis(ship,2000,2000,2000,2000,2000);
    seed.enable_individual_workforce();
    seed.upgrade_assignment_snapshot_format();
    seed.enable_passenger_life_support();
    PersistentWorld world(db,std::move(seed));
    ServerViewBridge bridge(world.world(),70077,7,local);
    ClientView client;
    CHECK(client.apply(decode_frame(encode_frame(bridge.snapshot())))==
          ApplyResult::Applied);
    CHECK(client.world().entities.count(foreign)==1);
    CHECK(client.world().entities.at(foreign).owner_id==0);
    CHECK(world.world().find(foreign)->owner_id==8);
    auto committed=[&](PlayerId owner,EntityId actor,EntityId target,
                      Action action,std::int64_t amount,std::uint64_t id){
        original_command(world,owner,actor,target,action,amount,id);
        CHECK(client.apply(decode_frame(encode_frame(
            bridge.after_committed_step(world.world()))))==ApplyResult::Applied);
    };
    committed(8,foreign,asteroid,Action::Mine,2,99001);
    committed(8,foreign,0,Action::RefineOre,2,99002);
    committed(8,foreign,0,Action::FabricateModule,1,99003);
    committed(8,foreign,0,Action::ConfigureCyberModule,5,99004);
    CHECK(world.world().find(foreign)->fitting.stealth_baffle());
    CHECK(client.world().entities.count(foreign)==0);
    // The original game knows ownership, but the player's stream NEVER
    // exposes an undetectable silhouette, foreign wallet or owner.
    CHECK(world.world().find(foreign)->owner_id==8);
    CHECK(client.world().wallets.count(8)==0);
    CHECK(client.world().entities.count(local)==1);
    committed(7,local,asteroid,Action::Mine,2,99005);
    committed(7,local,0,Action::RefineOre,2,99006);
    committed(7,local,0,Action::FabricateModule,1,99007);
    committed(7,local,0,Action::ConfigureCyberModule,6,99008);
    CHECK(world.world().find(local)->fitting.counter_ecm_array());
    CHECK(client.world().entities.count(foreign)==1);
    CHECK(client.world().entities.at(foreign).owner_id==0);
    // A foreign owner sees its own real vessel and inventory. This
    // projection redaction is NOT a mutation of original SQLite authority.
    ServerViewBridge foreign_bridge(world.world(),70078,8,foreign);
    ClientView foreign_view;
    CHECK(foreign_view.apply(decode_frame(
          encode_frame(foreign_bridge.snapshot())))==ApplyResult::Applied);
    CHECK(foreign_view.world().entities.at(foreign).owner_id==8);
    committed(7,local,0,Action::ConfigureCyberModule,-6,99009);
    CHECK(client.world().entities.count(foreign)==0);
    CHECK(world.world().find(foreign)->owner_id==8);
    const auto saved=world.world().canonical_state();
    CHECK(Simulation::from_snapshot(saved).canonical_state()==saved);
    fs::remove_all(dir);
    std::cout<<"PASS SHIP-STATUS-07: real viewer removal and rediscovery under stealth, anonymous foreign tracks, true original ownership and v103 world authority\n";
    return 0;
 }catch(const std::exception& e){
    std::cerr<<"FAIL SHIP-STATUS-07 viewer privacy: "<<e.what()<<'\n';
    std::error_code ignored;fs::remove_all(dir,ignored);
    return 1;
 }
}
