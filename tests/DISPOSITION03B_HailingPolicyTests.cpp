#include "galaxy/TerritorialHailing.hpp"
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <iostream>
using namespace galaxy;
using namespace galaxy::colony_disposition;
#define CHECK(x) do {if(!(x))throw std::runtime_error(std::string(#x)+" at "+std::to_string(__LINE__));}while(false)
int main() {
 namespace fs=std::filesystem;
 const auto path=(fs::temp_directory_path()/("sg_hail03b_"+std::to_string(
    std::chrono::steady_clock::now().time_since_epoch().count())+".sqlite")).string();
 const auto clean=[&]{for(const auto* s:{"","-wal","-shm"})fs::remove(path+s);};
 try {
  {
  Simulation seed(30304);
  const auto colony=seed.add_colony_ship(7,{0,0},100,6);
  const auto visitor=seed.add_ship(8,{1000,0},100);
  seed.provision_colony_genesis(colony,2000,2000,2000,2000,2000);
  seed.enable_individual_workforce();
  seed.upgrade_assignment_snapshot_format();
  seed.enable_passenger_life_support();
  PersistentWorld world(path,std::move(seed));
  TerritoryJournal journal(path);
  CHECK(journal.claim(world,7,colony,63001,3000));
  CHECK(player_territorial_hail(world,journal,8,colony,visitor).action==HailAction::AutomaticHail);
  CHECK(player_territorial_hail(world,journal,8,colony,visitor,{false}).action==HailAction::ManualHailAvailable);
  CHECK(player_territorial_hail(world,journal,9,colony,visitor).action==HailAction::None);
  std::uint64_t guard=0;
  for(const auto& [id,n]:world.world().npc_records())
   if(n.home_ship==colony && n.location==colony &&
      world.world().npc_available_for_duty(id)){guard=id;break;}
  CHECK(guard);
  CHECK(journal.warn(world,7,colony,visitor,guard,63002,5));
  CHECK(player_territorial_hail(world,journal,8,colony,visitor,{false}).action==HailAction::AwaitingVisitorResponse);
  CHECK(!player_respond_to_hail(world,journal,9,colony,visitor,63002,63003,HailChoice::Acknowledge));
  CHECK(!territorial_hail_aggravation(journal,{1,world.world().tick()+1,8,visitor,colony,true}).apply);
  CHECK(!player_respond_to_hail(world,journal,8,colony,visitor,63002,63002,HailChoice::Acknowledge));
  CHECK(player_respond_to_hail(world,journal,8,colony,visitor,63002,63003,HailChoice::Acknowledge));
  CHECK(!player_respond_to_hail(world,journal,8,colony,visitor,63002,63004,HailChoice::Refuse));
  CHECK(player_territorial_hail(world,journal,8,colony,visitor).actual_receipt_proven);
  const auto tick=world.world().tick();
  CHECK(!territorial_hail_aggravation(journal,{2,tick,8,visitor,colony,true}).apply);
  CHECK(!territorial_hail_aggravation(journal,{2,tick+1,8,visitor,colony,false}).apply);
  CHECK(!territorial_hail_aggravation(journal,{2,tick+1,9,visitor,colony,true}).apply);
  CHECK(!territorial_hail_aggravation(journal,{2,tick+1,8,visitor,colony+123,true}).apply);
  const auto aggravated=territorial_hail_aggravation(journal,{2,tick+1,8,visitor,colony,true});
  CHECK(aggravated.apply && aggravated.numerator==3 && aggravated.denominator==2);
  CHECK(aggravated.notice_event==63002 && aggravated.visitor_reply_event==63003);
  CHECK(suggested_sanction_units(10,aggravated,100)==15);
  CHECK(suggested_sanction_units(11,aggravated,100)==17);
  CHECK(suggested_sanction_units(90,aggravated,100)==100);
  CHECK(suggested_sanction_units(10,{},100)==10);
  journal.verify();
  }
  clean();
  std::cout<<"PASS DISPOSITION-03B: opt-out, owned hails, authenticated receipt, proven offense and capped 1.5x policy\n";
  return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL DISPOSITION-03B "<<e.what()<<'\n';clean();return 1;}
}
