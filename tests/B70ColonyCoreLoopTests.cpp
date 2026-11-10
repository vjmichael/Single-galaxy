#include "galaxy/Persistence.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define REQUIRE(x) do{ if(!(x)) throw std::runtime_error(std::string("check failed: ")+ #x +" @line "+std::to_string(__LINE__));}while(0)
static fs::path temporary(const char* label){
    auto root=fs::temp_directory_path()/(std::string("sg_b70_")+label+"_"+
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);return root/"colony.sqlite";
}
static Command command(std::uint64_t id,const Simulation& s,EntityId actor,Action a,
                       std::int64_t amount=1,PlayerId owner=7,EntityId target=0){
    Command c{}; c.id=id;c.sequence=id;c.execute_tick=s.tick()+1;c.issuer=owner;
    c.actor=actor;c.target=target;c.action=a;c.amount=amount;return c;
}
static Event apply(PersistentWorld& w,Command c){
    const auto before=w.world().events().size();REQUIRE(w.submit(c));w.step();
    REQUIRE(w.world().events().size()==before+1);
    return w.world().events().back();
}
int main(){
 try{
  const auto db=temporary("full");
  EntityId colony=0, asteroid=0,outpost=0;
  std::string saved;
  {
    Simulation initial(44);colony=initial.add_colony_ship(7,{0,0},60,24);
    asteroid=initial.add_asteroid({1800,0},40);
    initial.add_asteroid({15000,1000},20);
    REQUIRE(initial.entities().size()==3);
    REQUIRE(initial.colony_ship(colony)->crew==24);
    REQUIRE(initial.total_ore()==60);
    REQUIRE(initial.built_outposts().empty());
    REQUIRE(initial.canonical_state().find("galaxy-kernel-v7|")==0);
    REQUIRE(Simulation::from_snapshot(initial.canonical_state()).canonical_state()==initial.canonical_state());
    PersistentWorld w(db.string(),std::move(initial));
    auto total=w.resource_origin();REQUIRE(total==60);
    auto original=w.world().canonical_state();
    // Unauthorized account cannot convert someone else's resources.
    REQUIRE(apply(w,command(10,w.world(),colony,Action::RefineOre,1,8)).result==Outcome::Unauthorized);
    REQUIRE(w.world().canonical_state()!=original); // authorized denial is journaled
    REQUIRE(apply(w,command(11,w.world(),colony,Action::RefineOre)).result==Outcome::NoResources);
    // Resource operations can't be submitted with forged targets or oversized counts.
    REQUIRE(!w.submit(command(12,w.world(),colony,Action::RefineOre,1001)));
    REQUIRE(!w.submit(command(13,w.world(),colony,Action::ConstructOutpost,2)));
    REQUIRE(!w.submit(command(14,w.world(),colony,Action::FabricateModule,1,7,asteroid)));
    REQUIRE(apply(w,command(15,w.world(),colony,Action::Mine,16,7,asteroid)).transferred==16);
    REQUIRE(w.world().find(colony)->cargo==16);
    REQUIRE(w.world().find(asteroid)->ore==24);
    REQUIRE(apply(w,command(16,w.world(),colony,Action::RefineOre,16)).result==Outcome::Applied);
    REQUIRE(w.world().find(colony)->cargo==0);
    REQUIRE(w.world().colony_ship(colony)->refined==16);
    REQUIRE(apply(w,command(17,w.world(),colony,Action::FabricateModule,4)).transferred==8);
    REQUIRE(w.world().colony_ship(colony)->refined==8);
    REQUIRE(w.world().colony_ship(colony)->modules==4);
    REQUIRE(apply(w,command(18,w.world(),colony,Action::ConstructOutpost)).transferred==8);
    REQUIRE(w.world().built_outposts().size()==1);
    outpost=w.world().built_outposts().begin()->first;
    REQUIRE(w.world().find(outpost)->owner_id==7);
    REQUIRE(w.world().hangar(outpost,7)!=nullptr);
    REQUIRE(w.world().colony_ship(colony)->modules==0);
    REQUIRE(w.world().total_ore()==total);
    REQUIRE(apply(w,command(19,w.world(),colony,Action::ConstructOutpost)).result==Outcome::NoResources);
    saved=w.world().canonical_state();
    REQUIRE(saved.find("outpost|")!=std::string::npos);
    REQUIRE(Simulation::from_snapshot(saved).state_hash()==w.world().state_hash());
    // A second outpost is physically built only with newly manufactured parts.
    REQUIRE(apply(w,command(20,w.world(),colony,Action::FabricateModule,4)).result==Outcome::Applied);
    REQUIRE(apply(w,command(21,w.world(),colony,Action::ConstructOutpost)).result==Outcome::InvalidTarget); // overlap denial
    REQUIRE(w.world().total_ore()==total);
    // No automatic pre-existing gate or market, even after constructing the first platform.
    for(const auto& [id,e]:w.world().entities()){
        REQUIRE(e.type!=EntityType::Stargate);
        REQUIRE(!w.world().market(id));
    }
    // Transaction rollback: a submitted command must not leak into a failed save.
    const auto unchanged=w.world().canonical_state();
    try{w.submit(command(22,w.world(),colony,Action::RefineOre),FailPoint::AfterReceiptWrite);
        throw std::runtime_error("fault injection did not throw");
    }catch(const std::runtime_error& e){REQUIRE(std::string(e.what()).find("injected failure")!=std::string::npos);}
    REQUIRE(w.world().canonical_state()==unchanged);
    REQUIRE(!w.world().has_command_id(22));
    REQUIRE(w.world().total_ore()==total);
    const auto copy=temporary("recovery");
    PersistentWorld::recover_copy(db.string(),copy.string());
    PersistentWorld restored(copy.string(),Simulation(1));
    REQUIRE(restored.world().canonical_state()==w.world().canonical_state());
    REQUIRE(restored.world().built_outposts().count(outpost)==1);
    fs::remove_all(copy.parent_path());
  }
  {
    PersistentWorld w(db.string(),Simulation(3));
    REQUIRE(w.world().colony_ship(colony)->crew==24);
    REQUIRE(w.world().built_outposts().count(outpost));
    REQUIRE(w.world().total_ore()==60);
    // Cannot refine without raw material, even when refined reserves exist.
    REQUIRE(apply(w,command(23,w.world(),colony,Action::RefineOre)).result==Outcome::NoResources);
  }
  fs::remove_all(db.parent_path());
  // Full hold check must count refined material and manufactured modules.
  {
    Simulation s(0xABC);auto ship=s.add_colony_ship(7,{0,0},8,3);
    auto rock=s.add_asteroid({1000,0},20);
    Command c=command(30,s,ship,Action::Mine,8,7,rock);
    REQUIRE(s.submit(c));s.step();
    REQUIRE(s.find(ship)->cargo==8);
    c=command(31,s,ship,Action::RefineOre,8);REQUIRE(s.submit(c));s.step();
    REQUIRE(s.find(ship)->cargo==0 && s.colony_ship(ship)->refined==8);
    c=command(32,s,ship,Action::Mine,1,7,rock);REQUIRE(s.submit(c));s.step();
    REQUIRE(s.events().back().result==Outcome::CargoFull);
    c=command(33,s,ship,Action::FabricateModule,4);REQUIRE(s.submit(c));s.step();
    REQUIRE(s.colony_ship(ship)->modules==4 && s.colony_ship(ship)->refined==0);
    c=command(34,s,ship,Action::Mine,1,7,rock);REQUIRE(s.submit(c));s.step();
    REQUIRE(s.events().back().result==Outcome::CargoFull);
    c=command(35,s,ship,Action::ConstructOutpost);REQUIRE(s.submit(c));s.step();
    REQUIRE(s.events().back().result==Outcome::Applied);
    c=command(36,s,ship,Action::Mine,1,7,rock);REQUIRE(s.submit(c));s.step();
    REQUIRE(s.events().back().result==Outcome::Applied);
  }
  // Two-person colony can refine but cannot build with fewer than three crew.
  {
    Simulation s(2);auto ship=s.add_colony_ship(7,{0,0},20,2);
    Command c=command(40,s,ship,Action::ConstructOutpost);REQUIRE(s.submit(c));s.step();
    REQUIRE(s.events().back().result==Outcome::InsufficientCrew);
    c=command(41,s,ship,Action::Thrust,0);c.thrust_x=1;REQUIRE(s.submit(c));s.step();
    REQUIRE(s.events().back().result==Outcome::Applied);
    REQUIRE(s.find(ship)->velocity.x==Simulation::kAccelerationMmPerTick2);
    // Snapshot tamper: reject fabricated resources and invalid crew counts.
    auto bad=s.canonical_state();
    auto pos=bad.find("colony|"+std::to_string(ship)+"|2|0|0");REQUIRE(pos!=std::string::npos);
    bad.replace(pos,std::string("colony|"+std::to_string(ship)+"|2|0|0").size(),
                "colony|"+std::to_string(ship)+"|0|0|0");
    try{(void)Simulation::from_snapshot(bad);throw std::runtime_error("tamper accepted");}
    catch(const std::runtime_error& ex){REQUIRE(std::string(ex.what()).find("snapshot:")!=std::string::npos);}
  }
  std::cout<<"PASS B7.0a colony-first start; owner/crew rules; mine/refine/fabricate/build; material conservation; capacity; persistent recovery; tamper; rollback\n";
 }catch(const std::exception& ex){std::cerr<<"FAIL B7.0a "<<ex.what()<<'\n';return 1;}
}
