#include "galaxy/Simulation.hpp"
#include "galaxy/Persistence.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <algorithm>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(v) do{if(!(v))throw std::runtime_error("Check: " #v " line "+std::to_string(__LINE__));}while(0)
static Event cmd(PersistentWorld& p,EntityId id,std::uint64_t nonce,Action act,
                 EntityId target=0,std::int64_t amount=1,PlayerId owner=7){
    Command c{};c.id=nonce;c.sequence=nonce;c.execute_tick=p.world().tick()+1;
    c.actor=id;c.issuer=owner;c.action=act;c.target=target;c.amount=amount;
    CHECK(p.submit(c));p.step();return p.world().events().back();
}
static std::int64_t get(const std::map<std::string,std::int64_t>& m,const std::string& key){
    const auto it=m.find(key);return it==m.end()?0:it->second;
}
int main(){
 try{
    const auto path=fs::temp_directory_path()/("b70c_minerals_"+
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".sqlite");
    auto backup=path.string()+".recovered.sqlite";
    // Pending v9 process commands must have a durable-compatible envelope
    // even if no mineral cargo exists and execution will return NoResources.
    {
       Simulation queued(777);
       const auto pilot=queued.add_colony_ship(7,{0,0},20,24);
       Command early{};early.id=501;early.sequence=501;early.issuer=7;
       early.actor=pilot;early.target=8;early.amount=1000;
       early.action=Action::RefineMineral;early.execute_tick=1;
       CHECK(queued.submit(early));
       const auto state=queued.canonical_state();
       CHECK(state.rfind("galaxy-kernel-v9|",0)==0);
       auto restored=Simulation::from_snapshot(state);
       CHECK(restored.canonical_state()==state);
       restored.step();
       CHECK(restored.events().back().result==Outcome::NoResources);
    }
    EntityId ship=0,metal=0,stony=0,hydrated=0;std::int64_t original=0;
    {
      Simulation genesis(0xB70C);
      ship=genesis.add_colony_ship(7,{0,0},60,24);
      metal=genesis.add_geological_asteroid({1000,0},AsteroidClass::Metallic,55,5700);
      stony=genesis.add_geological_asteroid({-1000,0},AsteroidClass::Silicate,95,2900);
      hydrated=genesis.add_geological_asteroid({0,1000},AsteroidClass::Carbonaceous,70,1700);
      CHECK(genesis.entities().size()==4 && genesis.built_outposts().empty());
      CHECK(genesis.canonical_state().rfind("galaxy-kernel-v8|",0)==0);
      original=genesis.total_material_kg();
      PersistentWorld p(path.string(),std::move(genesis));
      auto bad=cmd(p,ship,1,Action::Mine,metal,12,8);CHECK(bad.result==Outcome::Unauthorized);
      CHECK(cmd(p,ship,2,Action::Mine,metal,12).transferred==12);
      CHECK(p.world().find(ship)->cargo==0);
      const auto* c=p.world().colony_ship(ship);
      CHECK(get(c->minerals_kg,"nickel_iron")==10320);
      CHECK(get(c->minerals_kg,"troilite")==960);
      CHECK(get(c->minerals_kg,"olivine")==360);
      CHECK(p.world().total_material_kg()==original);
      CHECK(p.world().canonical_state().rfind("galaxy-kernel-v9|",0)==0);
      CHECK(cmd(p,ship,3,Action::RefineMineral,8,10320).transferred==10320);
      c=p.world().colony_ship(ship);
      CHECK(get(c->minerals_kg,"nickel_iron")==0);
      CHECK(get(c->goods_kg,"structural_alloy")==9288 && c->tailings_kg==1032);
      CHECK(cmd(p,ship,4,Action::FabricateMaterialModule,1,5).result==Outcome::NoResources);
      CHECK(cmd(p,ship,5,Action::FabricateMaterialModule,1,4).transferred==8);
      CHECK(p.world().colony_ship(ship)->modules==4);
      CHECK(p.world().total_material_kg()==original);
      CHECK(cmd(p,ship,6,Action::ConstructOutpost).result==Outcome::Applied);
      CHECK(p.world().built_outposts().size()==1);
      CHECK(p.world().total_material_kg()==original);
      CHECK(cmd(p,ship,7,Action::Mine,stony,10).transferred==10);
      c=p.world().colony_ship(ship);
      CHECK(get(c->minerals_kg,"plagioclase")==4000);
      CHECK(cmd(p,ship,8,Action::RefineMineral,10,4000).result==Outcome::Applied);
      c=p.world().colony_ship(ship);
      CHECK(get(c->goods_kg,"aluminum")==800);
      CHECK(get(c->goods_kg,"glass")==2400);
      CHECK(cmd(p,ship,9,Action::Mine,hydrated,5).transferred==5);
      CHECK(cmd(p,ship,10,Action::RefineMineral,1,2750).transferred==2750);
      c=p.world().colony_ship(ship);
      CHECK(get(c->goods_kg,"water")==275);
      CHECK(get(c->goods_kg,"ceramic")==1925);
      CHECK(p.world().total_material_kg()==original);
      CHECK(cmd(p,ship,11,Action::RefineMineral,10,1000).result==Outcome::NoResources);
      CHECK(cmd(p,ship,12,Action::FabricateMaterialModule,2,1).result==Outcome::NoResources);
      CHECK(p.world().total_material_kg()==original);
      auto snapshot=p.world().canonical_state();
      CHECK(Simulation::from_snapshot(snapshot).canonical_state()==snapshot);
      auto tamper=snapshot;const auto pos=tamper.find("mineral|"+std::to_string(ship)+"|troilite|");
      CHECK(pos!=std::string::npos);
      tamper.replace(pos+std::string("mineral|"+std::to_string(ship)+"|").size(),8,"cheating");
      bool rejected=false;try{(void)Simulation::from_snapshot(tamper);}catch(const std::exception&){rejected=true;}
      CHECK(rejected);
      PersistentWorld::recover_copy(path.string(),backup); // recovery-copy must rebuild material inventory from journal
    }
    {
      Simulation ignored(19);ignored.add_colony_ship(7,{0,0},60,24);
      PersistentWorld p(path.string(),std::move(ignored));
      CHECK(p.world().total_material_kg()==original);
      CHECK(p.world().built_outposts().size()==1);
      CHECK(get(p.world().colony_ship(ship)->goods_kg,"aluminum")==800);
      CHECK(get(p.world().colony_ship(ship)->goods_kg,"water")==275);
      CHECK(p.world().find(metal)->ore==p.world().geology(metal)->original_tonnes-12);
      CHECK(p.world().canonical_state().rfind("galaxy-kernel-v9|",0)==0);
    }
    {
      Simulation ignored(19);ignored.add_colony_ship(7,{0,0},60,24);
      PersistentWorld p(backup,std::move(ignored));
      CHECK(p.world().total_material_kg()==original);
      CHECK(get(p.world().colony_ship(ship)->goods_kg,"aluminum")==800);
    }
    fs::remove(path);fs::remove(backup);
    std::cout<<"PASS B7.0c physical mineral fractions, processing, tailings, structural modules, outpost, ledger, restart, recovery-copy\n";
    return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL B7.0c: "<<e.what()<<'\n';return 1;}
}
