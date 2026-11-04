#include "galaxy/Persistence.hpp"
#include "galaxy/NpcAppearanceGenome.hpp"
#include "galaxy/RegionalModeAuthority.hpp"
#include "galaxy/ColonySafety.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
#define CHECK(expr) do{if(!(expr))throw std::runtime_error(std::string("AUTO04D5A: ")+#expr+" at line "+std::to_string(__LINE__));}while(false)
static void sql(sqlite3* db,const std::string& statement){
    char* error=nullptr;
    if(sqlite3_exec(db,statement.c_str(),nullptr,nullptr,&error)!=SQLITE_OK){
        const std::string msg=error?error:"unknown";sqlite3_free(error);
        throw std::runtime_error("tamper SQL: "+msg);
    }
}
static void clean(const std::string& path){
    for(const auto* suffix:{"","-wal","-shm"})std::filesystem::remove(path+suffix);
}
int main(){
try{
    const auto db=(std::filesystem::temp_directory_path()/
        ("sg_auto_78_"+std::to_string(std::chrono::steady_clock::now()
            .time_since_epoch().count())+".sqlite")).string();
    const auto recovered=db+".recovered",tampered=db+".tampered";
    constexpr std::uint64_t seed=8278;
    const auto child=(1ULL<<62)+8278;
    const std::string family_scope="v78-npc-family",region_scope="reg-78";
    EntityId home=0,far=0,near=0;
    std::int64_t material=0;
    {
        Simulation sim(8278);
        home=sim.add_colony_ship(7,{0,0},100,2);
        far=sim.add_colony_ship(7,{1500,0},100,2);
        near=sim.add_colony_ship(7,{0,0},100,2);
        sim.provision_colony_genesis(home,100,100,100,200,500);
        sim.provision_colony_genesis(far,1000,1000,1000,1000,1000);
        sim.provision_colony_genesis(near,1000,1000,1000,1000,1000);
        sim.enable_individual_workforce();
        sim.upgrade_assignment_snapshot_format();
        sim.enable_passenger_life_support();
        material=sim.total_material_kg();
        PersistentWorld world(db,std::move(sim));
        {
            NpcAppearanceStore family(db,family_scope,seed,
                [&](auto id){return world.world().npc_records().count(id)!=0;});
            family.register_settlement(home,2);
            family.register_settlement(far,2);
            family.register_settlement(near,2);
            for(std::uint64_t id=1;id<=2;++id)family.enroll_family_founder(id,home);
            for(std::uint64_t id=3;id<=4;++id)family.enroll_family_founder(id,far);
            for(std::uint64_t id=5;id<=6;++id)family.enroll_family_founder(id,near);
        }
        namespace re=galaxy::regional_events;
        re::CheckpointStore region(db,region_scope);
        re::Scheduler sched;
        cold_colony::State census{};census.population=2;
        sched.add_colony("home",census);
        sched.add_colony("far",census);
        sched.add_colony("near",census);
        CHECK(region.save(sched,0)==1);
        re::RegionalModeAuthority owner(region);
        CHECK(owner.enroll().generation==1);
        SocietyBirthPlan birth{};
        birth.expected_world_generation=world.generation();
        birth.expected_region_revision=1;
        birth.expected_owner_generation=1;
        birth.family_seed=seed;
        birth.expected_family_revision=0;
        birth.event_id=8271;
        birth.child_id=child;
        birth.parent_a=1;birth.parent_b=2;
        birth.home_ship=home;birth.family_scope=family_scope;
        birth.region_scope=region_scope;birth.regional_colony="home";
        world.step_with_atomic_dependent_birth(birth);
        Command reg{};
        reg.id=8272;reg.sequence=8272;
        reg.issuer=Simulation::kServerAuthority;
        reg.actor=home;reg.target=home;
        reg.action=Action::HostRegisterDependent;
        reg.transport_id=child;reg.passenger_ids={1};
        reg.execute_tick=world.world().tick()+1;
        CHECK(world.submit_host_dependent_registration(reg,family_scope,seed));
        world.step_with_dependent_registration();
        world.steps(640-world.world().tick());
        CHECK(static_cast<int>(colony_safety::assess(world.world(),home,home).risk)>=
              static_cast<int>(colony_safety::Risk::Critical));
        CHECK(world.world().children_physically_aboard(home)==1);
        const auto notice=world.pending_colony_notifications(100);
        std::uint64_t witnessed=0;
        for(const auto& item:notice)
            if(item.npc_id==1 && item.site==home && item.risk>=2)
                witnessed=std::max(witnessed,item.event_id);
        CHECK(witnessed!=0);
        PersistentWorld::AutonomousRescueRequest config{};
        config.source=home;
        config.leader_id=1;config.observed_event_id=witnessed;
        config.family_seed=seed;
        config.intent_id=8273;config.command_id=8274;
        config.family_scope=family_scope;config.region_scope=region_scope;
        config.origin_colony="home";
        // The NPC chooses between TWO real ships, not a user-specified
        // destination. The nearer one wins with a grounded safe manifest.
        config.destinations={{far,"far"},{near,"near"}};
        config.member_event_ids={8281,8282,8283};
        CHECK(!world.assess_autonomous_colony_emergency(config).rescue_ready);
        CHECK(!world.submit_autonomous_colony_rescue(config));
        const auto unbound=config;
        CHECK(world.acknowledge_colony_notification(witnessed,1));
        CHECK(!world.acknowledge_colony_notification(witnessed,1));
        auto invalid=config;
        invalid.observed_event_id+=1000;
        CHECK(!world.assess_autonomous_colony_emergency(invalid).rescue_ready);
        invalid=config;
        invalid.member_event_ids[2]=invalid.member_event_ids[1];
        CHECK(!world.assess_autonomous_colony_emergency(invalid).rescue_ready);
        invalid=config;
        invalid.destinations={{far,"far"}};
        // Far remains feasible, just less optimal: no destination is forced
        // by the caller and the normal selector may choose it if alone.
        CHECK(world.assess_autonomous_colony_emergency(invalid).rescue_ready);
        const auto decision=world.assess_autonomous_colony_emergency(config);
        CHECK(decision.notified_leader);
        CHECK(decision.rescue_ready);
        CHECK(decision.selected_receiver==near);
        CHECK(decision.transfer.family.receiver==near);
        CHECK(decision.transfer.family.adult_ids==std::vector<std::uint64_t>({1,2}));
        CHECK(decision.transfer.family.children.size()==1);
        CHECK(decision.transfer.family.children.front().child_id==child);
        CHECK(decision.transfer.family.children.front().guardian_id==1);
        CHECK(decision.transfer.expected_region_revision==2);
        CHECK(decision.transfer.expected_owner_generation==1);
        const auto before=world.generation();
        bool injected=false;
        try{world.submit_autonomous_colony_rescue(config,FailPoint::AfterSnapshotWrite);}
        catch(const std::exception&){injected=true;}
        CHECK(injected);
        CHECK(world.generation()==before);
        CHECK(world.world().children_physically_aboard(home)==1);
        CHECK(!world.world().has_command_id(config.command_id));
        CHECK(world.assess_autonomous_colony_emergency(config).rescue_ready);
        CHECK(world.submit_autonomous_colony_rescue(config));
        CHECK(world.generation()==before+1);
        CHECK(world.world().children_physically_aboard(home)==1);
        CHECK(!world.verified_atomic_family_departure(config.command_id));
        CHECK(!world.submit_autonomous_colony_rescue(config));
        world.step_with_atomic_family_departure(decision.transfer);
        CHECK(world.verified_atomic_family_departure(config.command_id));
        CHECK(world.world().children_physically_aboard(home)==0);
        CHECK(world.world().children_physically_aboard(near)==1);
        CHECK(world.world().colony_ship(home)->crew==0);
        CHECK(world.world().colony_ship(far)->crew==2);
        CHECK(world.world().colony_ship(near)->crew==4);
        CHECK(world.world().physical_dependents().at(child).guardian_id==1);
        CHECK(world.world().npc_records().size()==6);
        CHECK(world.world().total_material_kg()==material);
        const auto checkpoint=region.load();
        CHECK(checkpoint.has_value());
        CHECK(checkpoint->revision==3);
        CHECK(checkpoint->scheduler.colony("home").population==0);
        CHECK(checkpoint->scheduler.colony("far").population==2);
        CHECK(checkpoint->scheduler.colony("near").population==5);
        const auto food=world.world().life_support(near)->food_kg;
        world.steps(660-world.world().tick());
        CHECK(world.world().life_support(near)->food_kg==food-5);
    }
    {
        PersistentWorld reopened(db,Simulation(8298));
        CHECK(reopened.verified_atomic_family_departure(8274));
        CHECK(reopened.world().physical_dependents().at(child).home_ship==near);
    }
    PersistentWorld::recover_copy(db,recovered);
    {
        PersistentWorld replay(recovered,Simulation(8299));
        CHECK(replay.verified_atomic_family_departure(8274));
        CHECK(replay.world().children_physically_aboard(near)==1);
    }
    PersistentWorld::recover_copy(db,tampered);
    {
        sqlite3* raw=nullptr;
        CHECK(sqlite3_open(tampered.c_str(),&raw)==SQLITE_OK);
        sql(raw,"DROP TRIGGER sg_colony_npc_auto_no_update");
        sql(raw,"UPDATE sg_colony_npc_autonomous_dispatch SET leader_id='999999' "
                "WHERE command_id='8274'");
        sqlite3_close(raw);
        bool denied=false;
        try{PersistentWorld corrupted(tampered,Simulation(8300));}
        catch(const std::runtime_error&){denied=true;}
        CHECK(denied);
    }
    clean(db);clean(recovered);clean(tampered);
    std::cout<<"PASS v78: delivered NPC critical warning, closest verified ship, "
        "atomic admission rollback, child+ART02+ECON03 custody, restart and corruption refusal\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
