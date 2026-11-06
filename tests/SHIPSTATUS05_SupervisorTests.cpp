#include "galaxy/NpcCyberSupervisor.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do {if(!(x))throw std::runtime_error(std::string(#x)+" line "+std::to_string(__LINE__));}while(false)

static Command order(const PersistentWorld& w,std::uint64_t id,PlayerId p,
                     EntityId ship,Action action,std::int64_t amount=0,
                     EntityId target=0) {
    Command c{};c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
    c.issuer=p;c.actor=ship;c.action=action;c.amount=amount;c.target=target;
    return c;
}
static void performed(PersistentWorld& w,const Command& c) {
    CHECK(w.submit(c));
    w.step();
    CHECK(w.world().events().back().command_id==c.id);
    CHECK(w.world().events().back().result==Outcome::Applied);
}
int main(){
 const auto dir=fs::temp_directory_path()/("sg_supervise05_"+std::to_string(
   std::chrono::steady_clock::now().time_since_epoch().count()));
 const auto db=(dir/"world.sqlite").string(),copy=(dir/"copy.sqlite").string();
 try {
    fs::create_directories(dir);
    EntityId player=0,predator=0,peace=0,rock=0;
    std::uint64_t pilot=0;
    std::uint64_t nonce=920000;
    std::string pending,performed_state;
    std::int64_t material=0;
    {
        Simulation genesis(91005);
        player=genesis.add_colony_ship(7,{1000,0},100,6);
        predator=genesis.add_colony_ship(8,{0,0},100,6);
        peace=genesis.add_colony_ship(9,{0,3000},100,6);
        rock=genesis.add_asteroid({400,0},20);
        genesis.provision_starter_exploration_scanner(player);
        for(auto ship:{player,predator,peace})
            genesis.provision_colony_genesis(ship,2000,2000,2000,2000,2000);
        genesis.enable_individual_workforce();
        genesis.upgrade_assignment_snapshot_format();
        genesis.enable_passenger_life_support();
        material=genesis.total_material_kg();
        PersistentWorld w(db,std::move(genesis));
        for(const auto& [id,n]:w.world().npc_records())
            if(n.home_ship==predator && n.location==predator &&
               n.role==NpcRole::Ship &&
               w.world().npc_available_for_duty(id)){pilot=id;break;}
        CHECK(pilot);
        // The predator has real resources but no installed cyber emitter.
        w.establish_colony_conflict_agenda(92601,predator,pilot,
            colony_conflict::Agenda::Predatory);
        std::uint64_t cmd=1001;
        performed(w,order(w,cmd++,8,predator,Action::Mine,2,rock));
        performed(w,order(w,cmd++,8,predator,Action::RefineOre,2));
        performed(w,order(w,cmd++,8,predator,Action::FabricateModule,1));
        CHECK(w.world().tick()==3);
        // Until physical installation, no NPC is allowed to hack.
        performed(w,order(w,cmd++,8,predator,Action::ConfigureCyberModule,3));
        CHECK(w.world().total_material_kg()==material);
        // At tick 4 the supervisor skips. Tick 5 triggers exactly one
        // original contact/strategic-intent/queued cyber command.
        CHECK(npc_cyber::supervise(w,7).queued==0);
        w.step();
        const auto first=npc_cyber::supervise(w,7);
        CHECK(first.tick==5 && first.queued==1 &&
              first.last_attacking_ship==predator);
        CHECK(first.last_target_ship!=0 && first.last_command>0);
        CHECK(w.world().npc_cyber_operators().count(first.last_command)==1);
        CHECK(npc_cyber::supervise(w,7).queued==0);
        CHECK(w.world().npc_cyber_operators().size()==1);
        pending=w.world().canonical_state();
        CHECK(pending.rfind("galaxy-kernel-v102|",0)==0);
        PersistentWorld::recover_copy(db,copy);
    }
    {
        PersistentWorld recovered(db,Simulation(2));
        CHECK(recovered.world().canonical_state()==pending);
        CHECK(recovered.world().npc_cyber_operators().size()==1);
        recovered.step(); // exact original scheduled NPC HackConfuse
        CHECK(recovered.world().events().back().result==Outcome::Applied);
        CHECK(recovered.world().status_effects().records().size()==1);
        CHECK(recovered.world().total_material_kg()==material);
        performed_state=recovered.world().canonical_state();
        const auto before=recovered.world().npc_cyber_operators().size();
        // Physical emission has not cooled down and old Confusion remains.
        while(recovered.world().tick()<10)recovered.step();
        CHECK(npc_cyber::supervise(recovered,7).queued==0);
        CHECK(recovered.world().npc_cyber_operators().size()==before);
        while(recovered.world().tick()<15)recovered.step();
        const auto second=npc_cyber::supervise(recovered,7);
        CHECK(second.queued==1);
        CHECK(recovered.world().npc_cyber_operators().size()==before+1);
        recovered.step();
        CHECK(recovered.world().events().back().result==Outcome::Applied);
        CHECK(recovered.world().status_effects().records().size()==2);
        CHECK(recovered.world().total_material_kg()==material);
        CHECK(recovered.world().find(player)->hull==100);
        CHECK(recovered.world().find(peace)->hull==100);
        CHECK(Simulation::from_snapshot(recovered.world().canonical_state())
              .canonical_state()==recovered.world().canonical_state());
    }
    {
        PersistentWorld independent(copy,Simulation(3));
        CHECK(independent.world().canonical_state()==pending);
        CHECK(independent.world().npc_cyber_operators().size()==1);
        independent.step();
        CHECK(independent.world().canonical_state()==performed_state);
        CHECK(independent.world().status_effects().records().size()==1);
    }
    fs::remove_all(dir);
    std::cout<<"PASS SHIP-STATUS-05: governed NPC recurrent physical attacks, finite cooldown, one-per-tick dedup and SQLite recovery\n";
    return 0;
 }catch(const std::exception& e){
    std::cerr<<"FAIL SHIP-STATUS-05 "<<e.what()<<'\n';
    std::error_code ignored;fs::remove_all(dir,ignored);
    return 1;
 }
}
