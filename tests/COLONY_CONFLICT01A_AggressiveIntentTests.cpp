#include "galaxy/Persistence.hpp"
#include "galaxy/ColonyConflict.hpp"
#include "galaxy/ColonyDefense.hpp"
#include <sqlite3.h>
#include <array>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace galaxy;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("CONFLICT01A: ")+ #x+" line "+std::to_string(__LINE__));}while(false)
static void sql(sqlite3* db,const std::string& cmd){
    char* err=nullptr;
    if(sqlite3_exec(db,cmd.c_str(),nullptr,nullptr,&err)!=SQLITE_OK){
        const std::string msg=err?err:"unknown";sqlite3_free(err);
        throw std::runtime_error(msg);
    }
}
static void clean(const std::string& p){
    for(const auto* e:{"","-wal","-shm"})std::filesystem::remove(p+e);
}
static Event action(PersistentWorld& w,EntityId ship,std::uint64_t id,
                    Action type,EntityId target=0,int amount=1){
    Command cmd{};cmd.id=id;cmd.sequence=id;cmd.issuer=7;
    cmd.actor=ship;cmd.target=target;cmd.action=type;cmd.amount=amount;
    cmd.execute_tick=w.world().tick()+1;
    CHECK(w.submit(cmd));w.step();
    return w.world().events().back();
}
static std::uint64_t first_resident(const Simulation& w,EntityId source){
    for(const auto& [id,npc]:w.npc_records())
        if(npc.home_ship==source && npc.location==source)return id;
    return 0;
}
int main(){
try{
    const auto path=(std::filesystem::temp_directory_path()/
        ("sg_npc_conflict_v82_"+std::to_string(std::chrono::steady_clock::now()
        .time_since_epoch().count())+".sqlite")).string();
    const auto recovered=path+".recovered",tampered=path+".tampered";
    EntityId predatory=0,expansion=0,desperate=0,isolation=0,cooperative=0,player=0;
    std::uint64_t saved_generation=0;
    {
        Simulation sim(8201);
        predatory=sim.add_colony_ship(7,{0,0},100,6);
        expansion=sim.add_colony_ship(9,{0,0},100,2);
        desperate=sim.add_colony_ship(10,{0,0},100,2);
        isolation=sim.add_colony_ship(11,{0,0},100,2);
        cooperative=sim.add_colony_ship(12,{0,0},100,2);
        player=sim.add_ship(8,{1000,0},100);
        sim.provision_colony_genesis(predatory,1000,1000,1000,1000,1000);
        sim.provision_colony_genesis(expansion,1000,1000,1000,1000,1000);
        sim.provision_colony_genesis(desperate,5,1000,1000,1000,1000);
        sim.provision_colony_genesis(isolation,1000,1000,1000,1000,1000);
        sim.provision_colony_genesis(cooperative,1000,1000,1000,1000,1000);
        const auto ore=sim.add_asteroid({2000,0},70);
        sim.enable_individual_workforce();
        sim.upgrade_assignment_snapshot_format();
        sim.enable_passenger_life_support();
        PersistentWorld world(path,std::move(sim));
        CHECK(action(world,predatory,1,Action::Mine,ore,60).result==Outcome::Applied);
        CHECK(action(world,predatory,2,Action::RefineOre,0,60).result==Outcome::Applied);
        CHECK(action(world,predatory,3,Action::FabricateModule,0,30).result==Outcome::Applied);
        CHECK(world.world().colony_ship(predatory)->modules>=2);
        world.steps(20-world.world().tick());
        CHECK(world.world().tick()==20);
        CHECK(world.world().life_support(desperate)->food_kg==3);
        CHECK(colony_safety::assess(world.world(),desperate,desperate).risk==
              colony_safety::Risk::Critical);
        struct Case {
            EntityId source;colony_conflict::Agenda agenda;
            colony_conflict::Motive motive;colony_conflict::Posture posture;
        };
        const std::array<Case,5> cases{{
            {predatory,colony_conflict::Agenda::Predatory,
                colony_conflict::Motive::Predation,colony_conflict::Posture::PrepareRaid},
            {expansion,colony_conflict::Agenda::Expansionist,
                colony_conflict::Motive::Territory,colony_conflict::Posture::Warn},
            {desperate,colony_conflict::Agenda::Cooperative,
                colony_conflict::Motive::Scarcity,colony_conflict::Posture::RequestAid},
            {isolation,colony_conflict::Agenda::Isolationist,
                colony_conflict::Motive::Security,colony_conflict::Posture::Warn},
            {cooperative,colony_conflict::Agenda::Cooperative,
                colony_conflict::Motive::None,colony_conflict::Posture::Observe}
        }};
        saved_generation=world.generation();
        for(std::size_t i=0;i<cases.size();++i){
            const auto& c=cases[i];
            const auto leader=first_resident(world.world(),c.source);
            CHECK(leader>0);
            CHECK(world.world().npc_available_for_duty(leader));
            world.establish_colony_conflict_agenda(8200+i,c.source,leader,c.agenda);
            const auto contacts=world.local_colony_watch_contacts(c.source,leader);
            const auto it=std::find_if(contacts.begin(),contacts.end(),
                [&](const auto& x){return x.foreign_ship==player;});
            CHECK(it!=contacts.end());
            CHECK(it->foreign_owner==8);
            const auto event=world.record_colony_watch_contact(8300+i,c.source,leader,player);
            CHECK(event.foreign_ship==player);
            const auto choice=world.assess_colony_conflict_intent(c.source,8300+i);
            CHECK(choice.source==c.source);
            CHECK(choice.leader_id==leader);
            CHECK(choice.target_ship==player);
            CHECK(choice.target_owner==8);
            CHECK(choice.motive==c.motive);
            CHECK(choice.posture==c.posture);
            const auto durable=world.record_colony_conflict_intent(8400+i,c.source,8300+i);
            CHECK(durable.posture==choice.posture);
            CHECK(durable.motive==choice.motive);
            CHECK(world.generation()==saved_generation);
        }
        // Never hallucinate an observed contact from another colony.
        bool refused=false;
        try{(void)world.assess_colony_conflict_intent(expansion,8300);}
        catch(const std::runtime_error&){refused=true;}
        CHECK(refused);
        // The same contact cannot be transformed into a different incident.
        refused=false;
        try{(void)world.record_colony_conflict_intent(8499,predatory,8300);}
        catch(const std::runtime_error&){refused=true;}
        CHECK(refused);
        CHECK(world.world().find(player)->type==EntityType::Ship);
        CHECK(world.world().colony_ship(predatory)->crew==6);
        CHECK(world.world().npc_records().size()==14);
        CHECK(world.world().npc_casualties().empty());
        world.steps(120-world.world().tick());
        CHECK(world.world().tick()==120);
        CHECK(world.world().find(player)->type==EntityType::Ship);
        CHECK(world.world().npc_casualties().empty());
    }
    {
        PersistentWorld replay(path,Simulation(8220));
        CHECK(replay.world().tick()==120);
        CHECK(replay.world().find(player)->type==EntityType::Ship);
        CHECK(replay.world().colony_ship(predatory)->modules>=2);
    }
    PersistentWorld::recover_copy(path,recovered);
    {
        PersistentWorld replay(recovered,Simulation(8221));
        CHECK(replay.world().tick()==120);
        CHECK(replay.world().npc_casualties().empty());
    }
    PersistentWorld::recover_copy(path,tampered);
    {
        sqlite3* raw=nullptr;
        CHECK(sqlite3_open(tampered.c_str(),&raw)==SQLITE_OK);
        sql(raw,"DROP TRIGGER sg_conflict_intent_no_update");
        sql(raw,"UPDATE sg_colony_conflict_intent SET posture=0 "
            "WHERE event_id=8400");
        sql(raw,"CREATE TRIGGER sg_conflict_intent_no_update "
            "BEFORE UPDATE ON sg_colony_conflict_intent "
            "BEGIN SELECT RAISE(ABORT,'immutable colony strategic intent'); END");
        sqlite3_close(raw);
        bool failed=false;
        try{PersistentWorld spoofed(tampered,Simulation(8222));}
        catch(const std::runtime_error&){failed=true;}
        CHECK(failed);
    }
    clean(path);clean(recovered);clean(tampered);
    std::cout<<"PASS CONFLICT01A: unprovoked predatory preparation, expansionist claims, "
      "scarcity pleas, security warnings, neutral contact, 120-tick replay and false-history rejection\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
