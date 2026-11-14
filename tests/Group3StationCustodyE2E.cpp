// PLAY-STATION-03B: actual original Simulation, actual PersistentWorld SQLite
// journal, physical ship->hangar ore lot, constructed site/utility/personnel.
#include "galaxy/Persistence.hpp"
#include "galaxy/StationMineralIdentityAudit.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sqlite3.h>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do { if(!(x)) throw std::runtime_error(std::string("PLAY-STATION-03B line ")+std::to_string(__LINE__)+": " #x); } while(0)
static Event send(PersistentWorld& w,std::uint64_t& id,EntityId ship,Action action,
                 EntityId target=0,std::int64_t amount=1,int selector=0,PlayerId owner=7,
                 FailPoint fail=FailPoint::None){
    Command c{};c.id=++id;c.sequence=c.id;c.execute_tick=w.world().tick()+1;
    c.issuer=owner;c.actor=ship;c.target=target;c.amount=amount;
    c.action=action;c.thrust_x=selector;
    CHECK(w.submit(c));
    w.step(fail);
    const auto result=w.world().events().back();
    CHECK(result.command_id==c.id);
    return result;
}
static void erase_db(const std::string& path){
    for(const auto* ext:{"","-wal","-shm"}){
        std::error_code ec;fs::remove(path+ext,ec);
    }
}
int main(int argc,char** argv){
 try{
  CHECK(argc==2 || (argc==3 &&
      (std::string(argv[2])=="--03c-fixture" || std::string(argv[2])=="--03c-verify" || std::string(argv[2])=="--04c-fixture")));
  const std::string db=argv[1],copy=db+".copy";
  const bool fixture=argc==3 && std::string(argv[2])=="--03c-fixture";
  const bool unload_fixture=argc==3 && std::string(argv[2])=="--04c-fixture";
  if(argc==3 && std::string(argv[2])=="--03c-verify"){
    PersistentWorld state(db,Simulation(12304));
    const auto& world=state.world();
    CHECK(world.player_people().at(7).state==PlayerPersonState::AtStation);
    const auto station=world.player_people().at(7).physical_station;
    const auto* refinery=world.station_refinery(station);
    CHECK(refinery && refinery->owner==7);
    CHECK(world.hangar(station,7) && world.hangar(station,7)->ore==0);
    CHECK(refinery->mineral_kg.at("nickel_iron")==860);
    CHECK(refinery->mineral_kg.at("troilite")==80);
    CHECK(refinery->mineral_kg.at("olivine")==30);
    CHECK(refinery->mineral_kg.at("pyroxene")==15);
    CHECK(refinery->mineral_kg.at("other")==15);
    std::int64_t total=0;
    for(const auto& [name,kg]:refinery->mineral_kg){(void)name;total+=kg;}
    CHECK(total==1000);
    for(const auto& [id,lot]:world.station_ore_lots()){
      (void)id;CHECK(lot.station!=station || lot.tonnes==0);
    }
    CHECK(Simulation::from_snapshot(world.canonical_state()).canonical_state()==world.canonical_state());
    std::cout<<"PLAY-STATION-03C VERIFY PASS station original SQLite 1000kg and no duplicate lot\n";
    return 0;
  }
  erase_db(db);erase_db(copy);
  std::uint64_t id=1000;EntityId ship=0,ship_b=0,station=0,rock=0,lot_id=0;
  std::int64_t initial_mass=0;
  {
    Simulation seed(12304);
    ship=seed.add_colony_ship(7,{0,0},100,24);
    // Additional finite owned hull in test genesis: never spawned as a rescue.
    ship_b=seed.add_ship(7,{0,0},10);
    seed.provision_colony_genesis(ship,1000,3000,1000,1000,1000);
    const auto bulk=seed.add_asteroid({2000,0},70);
    rock=seed.add_geological_asteroid({3000,0},AsteroidClass::Metallic,10,5000);
    seed.enable_native_galactic_physics({{1,galactic::Point{{0,100000},{0,100000},{0,100000}}}});
    seed.enable_station_ore_provenance();
    initial_mass=seed.total_material_kg();
    PersistentWorld w(db,std::move(seed));
    CHECK(send(w,id,ship,Action::Mine,bulk,60).result==Outcome::Applied);
    CHECK(w.world().find(ship)->cargo==60);
    CHECK(send(w,id,ship,Action::RefineOre,0,60).result==Outcome::Applied);
    CHECK(send(w,id,ship,Action::FabricateModule,0,30).result==Outcome::Applied);
    CHECK(send(w,id,ship,Action::ConstructOutpost,0,1).result==Outcome::Applied);
    station=w.world().built_outposts().begin()->first;
    CHECK(send(w,id,ship,Action::ConstructUtility,station).result==Outcome::Applied);
    CHECK(send(w,id,ship,Action::ConstructHabitat,station).result==Outcome::Applied);
    CHECK(send(w,id,ship,Action::TransferHabitatStock,station,100,1).result==Outcome::Applied);
    CHECK(send(w,id,ship,Action::TransferHabitatStock,station,120,2).result==Outcome::Applied);
    CHECK(send(w,id,ship,Action::TransferHabitatStock,station,100,3).result==Outcome::Applied);
    CHECK(send(w,id,ship,Action::AssignHabitatResidents,station,3).result==Outcome::Applied);
    CHECK(send(w,id,ship,Action::AssignUtilityTechnicians,station,2).result==Outcome::Applied);
    CHECK(send(w,id,ship,Action::RequestDock,station).result==Outcome::Applied);
    w.steps(2);
    CHECK(w.world().find(ship)->dock_state==DockState::Docked);
    CHECK(send(w,id,ship,Action::BuildStationRefinery,station,1,0,8).result==Outcome::Unauthorized);
    CHECK(!w.world().station_refinery(station));
    CHECK(send(w,id,ship,Action::BuildStationRefinery,station).result==Outcome::Applied);
    CHECK(send(w,id,ship,Action::BuildStationRefinery,station).result==Outcome::InvalidTarget);
    CHECK(w.world().station_refinery(station));
    CHECK(w.world().station_refinery(station)->embodied_modules==2);
    CHECK(w.world().utility(station)->technicians==1);
    CHECK(send(w,id,ship,Action::RequestUndock,station).result==Outcome::Applied);
    w.steps(2);
    CHECK(w.world().find(ship)->dock_state==DockState::InSpace);
    const auto mined=send(w,id,ship,Action::Mine,rock,1);
    CHECK(mined.result==Outcome::Applied && mined.transferred==1);
    CHECK(w.world().find(ship)->cargo==1);
    CHECK(w.world().colony_ship(ship)->minerals_kg.empty());
    CHECK(!w.world().station_ore_lots().empty());
    lot_id=w.world().station_ore_lots().rbegin()->first;
    CHECK(w.world().station_ore_lots().at(lot_id).rock==rock);
    CHECK(w.world().station_ore_lots().at(lot_id).station==0);
    CHECK(w.world().total_material_kg()==initial_mass);
    CHECK(send(w,id,ship,Action::RequestDock,station).result==Outcome::Applied);
    w.steps(2);
    if(unload_fixture){
      // A real one-tonne geological Mine already committed above. Keep its
      // actual cargo on this docked hull for live original-host unloading.
      CHECK(w.register_player_at_station(7,station));
      w.steps(35);
      CHECK(w.world().find(ship)->cargo==1);
      CHECK(w.world().hangar(station,7)->ore==0);
      CHECK(w.world().station_ore_lots().at(lot_id).station==0);
      std::cout<<"UNLOAD_FIXTURE|"<<station<<'|'<<lot_id<<'|'<<ship
               <<'|'<<initial_mass<<'\n';
      return 0;
    }
    CHECK(send(w,id,ship,Action::RefineStationOre,station,lot_id).result==Outcome::InvalidTarget);
    CHECK(send(w,id,ship,Action::UnloadOre,station,1).result==Outcome::Applied);
    CHECK(w.world().hangar(station,7)->ore==1);
    CHECK(w.world().find(ship)->cargo==0);
    CHECK(w.world().station_ore_lots().at(lot_id).station==station);
    CHECK(w.world().station_ore_lots().at(lot_id).unload_receipt!=0);
    CHECK(w.world().total_material_kg()==initial_mass);
    // Two real owned ships may dock at the same original station, but a
    // second hull cannot load tagged geological ore with another source hull.
    CHECK(send(w,id,ship_b,Action::RequestDock,station).result==Outcome::Applied);
    w.steps(2);
    CHECK(w.world().find(ship_b)->dock_state==DockState::Docked);
    CHECK(send(w,id,ship_b,Action::LoadOre,station,1).result==Outcome::Unauthorized);
    CHECK(w.world().hangar(station,7)->ore==1);
    CHECK(w.world().find(ship_b)->cargo==0);
    CHECK(w.world().station_ore_lots().at(lot_id).source_ship==ship);
    CHECK(w.world().total_material_kg()==initial_mass);
    CHECK(w.register_player_at_station(7,station));
    w.steps(35); // utility generation and site staff are real simulation ticks
    const auto* u=w.world().utility(station);
    std::cout<<"utility powered="<<u->powered<<" energy="<<u->stored_energy<<" cond="<<u->condition<<"\n";
    CHECK(u->powered && u->stored_energy>0);
    if(fixture){
      std::cout<<"FIXTURE|"<<station<<'|'<<lot_id<<'|'<<ship<<'|'<<initial_mass<<"\n";
      return 0; // persist the owned, built, powered station and original ore lot
    }
    auto wrong=send(w,id,ship,Action::RefineStationOre,station,lot_id,0,8);
    CHECK(wrong.result==Outcome::Unauthorized);
    CHECK(w.world().hangar(station,7)->ore==1);
    Command pending{};pending.id=++id;pending.sequence=pending.id;
    pending.issuer=7;pending.actor=ship;pending.target=station;
    pending.amount=static_cast<std::int64_t>(lot_id);
    pending.action=Action::RefineStationOre;
    pending.execute_tick=w.world().tick()+1;
    CHECK(w.submit(pending));
    const auto after_admission=w.generation();
    try{w.step(FailPoint::AfterSnapshotWrite);CHECK(false);}
    catch(const std::exception&){}
    CHECK(w.generation()==after_admission && w.world().hangar(station,7)->ore==1);
    w.step();
    const auto refined=w.world().events().back();
    CHECK(refined.command_id==pending.id);
    CHECK(refined.result==Outcome::Applied && refined.transferred==1000);
    CHECK(w.world().hangar(station,7)->ore==0);
    CHECK(!w.world().station_ore_lots().count(lot_id));
    CHECK(w.world().station_refinery(station)->mineral_kg.at("nickel_iron")==860);
    CHECK(w.world().station_refinery(station)->mineral_kg.at("troilite")==80);
    CHECK(w.world().station_refinery(station)->mineral_kg.at("olivine")==30);
    CHECK(w.world().station_refinery(station)->mineral_kg.at("pyroxene")==15);
    CHECK(w.world().station_refinery(station)->mineral_kg.at("other")==15);
    // The named original minerals are real STATION stock; the signed
    // source-lot receipt is not an extra 1000kg. No implicit aluminium or
    // other candidate feed appears in the player ship inventory.
    const auto mineral_report=station_material_identity::audit_original_station(
        w.world(),station,7);
    CHECK(mineral_report && mineral_report->stock_kilograms==1000);
    CHECK(mineral_report->last_refinery_receipt==refined.command_id);
    CHECK(mineral_report->stock.size()==5);
    for(const auto& row:mineral_report->stock)
        CHECK(row.grams==row.kilograms*1000 && !row.approved_industry_feed);
    CHECK(!station_material_identity::audit_original_station(w.world(),station,8));
    CHECK(w.world().colony_ship(ship)->goods_kg.count("aluminum")==0);
    CHECK(w.world().total_material_kg()==initial_mass);
    CHECK(send(w,id,ship,Action::RefineStationOre,station,lot_id).result==Outcome::InvalidTarget);
    const auto state=w.world().canonical_state();
    CHECK(state.rfind("galaxy-kernel-v123|",0)==0);
    CHECK(Simulation::from_snapshot(state).canonical_state()==state);
    PersistentWorld::recover_copy(db,copy);
  }
  for(const auto& path:{db,copy}){
    PersistentWorld w(path,Simulation(12304));
    CHECK(w.world().station_refinery(station));
    CHECK(w.world().station_refinery(station)->mineral_kg.at("nickel_iron")==860);
    CHECK(w.world().hangar(station,7)->ore==0);
    CHECK(w.world().find(ship)->cargo==0);
    CHECK(w.world().total_material_kg()==initial_mass);
    const auto restored=station_material_identity::audit_original_station(w.world(),station,7);
    CHECK(restored && restored->stock_kilograms==1000 && restored->stock.size()==5);
    CHECK(Simulation::from_snapshot(w.world().canonical_state()).canonical_state()==w.world().canonical_state());
  }
  sqlite3* con=nullptr;CHECK(sqlite3_open(copy.c_str(),&con)==SQLITE_OK);
  char* err=nullptr;
  const auto rc=sqlite3_exec(con,
    "UPDATE resource_ledger SET amount=amount+1 WHERE reason='station_refinery_process_kg'",
    nullptr,nullptr,&err);
  if(err){std::cerr<<err<<'\n';sqlite3_free(err);}
  CHECK(rc==SQLITE_OK && sqlite3_changes(con)==1);
  sqlite3_close(con);
  bool caught=false;
  try{PersistentWorld invalid(copy,Simulation(12305));(void)invalid;}
  catch(const std::exception&){caught=true;}
  CHECK(caught);
  erase_db(db);erase_db(copy);
  std::cout<<"GROUP3 RESOURCE→REFINERY PASS original mine source lot -> owned docked hangar -> existing built refinery + real utility staff/power -> 1000kg station minerals; SQLite restart, rollback, forged ledger refusal, no duplicate\n";
  return 0;
 }catch(const std::exception& ex){std::cerr<<"PLAY-STATION-03B FAILED: "<<ex.what()<<'\n';return 1;}
}
