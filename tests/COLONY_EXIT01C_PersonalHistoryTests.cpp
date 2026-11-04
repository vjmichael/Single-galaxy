#include "galaxy/Persistence.hpp"
#include "galaxy/ColonySafety.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("EXIT01C line ")+std::to_string(__LINE__)+": " #x);}while(false)
template<class F>static void refused(F&& f){bool ok=false;try{f();}catch(const std::exception&){ok=true;}CHECK(ok);}
static void clean(const std::string& p){for(const auto* s:{"","-wal","-shm"})std::filesystem::remove(p+s);}
static void execute(PersistentWorld& w,EntityId colony,std::uint64_t id,
                    Action action,std::int64_t amount){
    Command c{};c.id=id;c.sequence=id;c.issuer=7;c.actor=colony;
    c.action=action;c.execute_tick=w.world().tick()+1;c.amount=amount;
    CHECK(w.submit(c));w.step();
    CHECK(!w.world().events().empty());
    const auto& outcome=w.world().events().back();
    CHECK(outcome.command_id==id && outcome.result==Outcome::Applied);
}
static void sql(sqlite3* db,const std::string& cmd){
    char* err=nullptr;if(sqlite3_exec(db,cmd.c_str(),nullptr,nullptr,&err)!=SQLITE_OK){
        std::string why=err?err:"SQL execution failed";sqlite3_free(err);
        throw std::runtime_error(why);
    }
}
int main(){
try{
    const auto db=(std::filesystem::temp_directory_path()/
        ("sg_exit_personal_"+std::to_string(std::chrono::steady_clock::now()
            .time_since_epoch().count())+".sqlite")).string();
    const auto recovered=db+".recovered",forged=db+".forged";
    EntityId source=0;
    {
        Simulation initial(8801);
        source=initial.add_colony_ship(7,{0,0},100,4);
        // Food and oxygen remain healthy, but the water supply will fail
        // genuinely before tick 250, leaving real consumed wastewater which
        // can be reclaimed rather than magically provisioned.
        initial.provision_colony_genesis(source,10000,220,10000,1000,5000);
        initial.enable_individual_workforce();
        initial.upgrade_assignment_snapshot_format();
        initial.enable_passenger_life_support();
        PersistentWorld w(db,std::move(initial));
        w.establish_colony_conflict_agenda(
            8801,source,1,colony_conflict::Agenda::Cooperative);
        CHECK(!w.colony_personal_outlook(source,2));
        w.steps(250-w.world().tick());
        CHECK(static_cast<int>(colony_safety::assess(w.world(),source,source).risk)>=2);
        CHECK(w.record_colony_exit_pressure(8810,source,2).npc_id==2);
        const auto first=w.record_personal_outlook(8820,source,2);
        CHECK(first.prior_event_id==0);
        CHECK(first.discontent>0);
        CHECK(!first.considering_departure);
        CHECK(!w.assess_personal_outlook(source,1)); // incumbent leader excluded
        CHECK(!w.assess_personal_outlook(source,99999));
        refused([&]{w.record_personal_outlook(8821,source,2);});
        CHECK(!w.assess_personal_outlook(source,2) ||
              w.assess_personal_outlook(source,2)->discontent==first.discontent);
        w.steps(8);
        const auto second=w.record_personal_outlook(8822,source,2);
        CHECK(second.prior_event_id==8820);
        CHECK(second.discontent>first.discontent);
        CHECK(second.considering_departure);
        CHECK(w.colony_personal_outlook(source,2)->considering_departure);
        CHECK(w.world().colony_ship(source)->crew==4);
        // The player performs ACTUAL loss-preserving water reclamation. This
        // resource-backed world command increases local water; no test code
        // edits a live Simulation directly.
        const auto* life=w.world().life_support(source);
        CHECK(life && life->wastewater_kg>=150);
        execute(w,source,8830,Action::RecycleWater,150);
        CHECK(colony_safety::assess(w.world(),source,source).risk==
              colony_safety::Risk::Safe);
        // Hysteresis: one good observation cannot erase years of harm.
        w.steps(8);
        const auto recovering=w.record_personal_outlook(8823,source,2);
        CHECK(recovering.discontent<second.discontent);
        CHECK(recovering.considering_departure);
        w.steps(8);
        const auto recovering_again=w.record_personal_outlook(8824,source,2);
        CHECK(recovering_again.discontent<recovering.discontent);
        CHECK(recovering_again.considering_departure);
        w.steps(8);
        const auto settled=w.record_personal_outlook(8825,source,2);
        CHECK(settled.discontent<30);
        CHECK(!settled.considering_departure);
        CHECK(settled.prior_event_id==8824);
        CHECK(w.colony_personal_outlook(source,2)->event_id==8825);
        CHECK(!w.colony_personal_outlook(source,2)->considering_departure);
        CHECK(w.world().colony_ship(source)->crew==4);
        CHECK(w.world().npc_casualties().empty());
        w.steps(340-w.world().tick());
        CHECK(w.world().tick()==340);
    }
    {
        PersistentWorld w(db,Simulation(8802));
        CHECK(w.colony_personal_outlook(source,2)->prior_event_id==8824);
        CHECK(!w.colony_personal_outlook(source,2)->considering_departure);
        CHECK(w.world().npc_records().at(2).home_ship==source);
    }
    PersistentWorld::recover_copy(db,recovered);
    {
        PersistentWorld w(recovered,Simulation(8803));
        CHECK(!w.colony_personal_outlook(source,2)->considering_departure);
        CHECK(w.colony_personal_outlook(source,2)->event_id==8825);
        CHECK(w.world().colony_ship(source)->crew==4);
    }
    PersistentWorld::recover_copy(db,forged);
    {
        sqlite3* raw=nullptr;CHECK(sqlite3_open(forged.c_str(),&raw)==SQLITE_OK);
        sql(raw,"DROP TRIGGER sg_personal_outlook_no_update");
        sql(raw,"UPDATE sg_colony_npc_personal_outlook SET discontent=100,"
            "considering_departure=1 WHERE event_id=8825");
        sql(raw,"CREATE TRIGGER sg_personal_outlook_no_update "
            "BEFORE UPDATE ON sg_colony_npc_personal_outlook "
            "BEGIN SELECT RAISE(ABORT,'immutable dated personal history'); END");
        sqlite3_close(raw);
        refused([&]{PersistentWorld impossible(forged,Simulation(8804));});
    }
    clean(db);clean(recovered);clean(forged);
    std::cout<<"PASS EXIT01C: one NPC's immutable time-spaced lived scarcity,"
        " actual physically recycled water, gradual recovery and rescinded exit"
        " intention, population conservation, restart, independent recovery,"
        " forged personal mood rejected\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"EXIT01C failure: "<<e.what()<<'\n';return 1;}
}
