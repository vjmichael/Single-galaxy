#include "galaxy/RegionalPopulationAudit.hpp"
#include "galaxy/NpcAppearanceGenome.hpp"
#include "galaxy/RegionalCheckpointStore.hpp"
#include "galaxy/RegionalModeAuthority.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace galaxy;
namespace re=galaxy::regional_events;
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class F>static void refuse(F task,const char* why){
    bool failed=false;try{task();}catch(const std::exception&){failed=true;}
    check(failed,why);
}
int main(){
    const auto path=(std::filesystem::temp_directory_path()/"sg_v49_shared_region.db").string();
    std::filesystem::remove(path);
    try{
        Simulation initial(9919);
        const auto ship=initial.add_colony_ship(7,{0,0},100,6);
        initial.enable_individual_workforce();initial.upgrade_assignment_snapshot_format();
        initial.enable_medical_workforce_rules();
        PersistentWorld w(path,std::move(initial));
        for(const auto& entry:std::vector<std::pair<std::string,std::vector<std::uint64_t>>>{
              {"family-east",{1,2,3}},{"family-west",{4,5,6}}}){
            NpcAppearanceStore fam(path,entry.first,9919,[&](auto id){
                return w.world().npc_records().count(id)!=0;});
            fam.register_settlement(ship,3);
            for(const auto id:entry.second)fam.enroll_family_founder(id,ship);
        }
        re::CheckpointStore region(path,"real-region");
        re::Scheduler scheduled;cold_colony::State stock{};stock.population=6;
        scheduled.add_colony("home",stock);
        check(region.save(scheduled,0)==1,"real checkpoint creation failed");
        re::RegionalModeAuthority auth(region);
        check(auth.enroll().generation==1,"real region authority enrollment failed");
        const auto good=[&](std::uint64_t revision,std::uint64_t owner){
            return verify_regional_population(w,path,ship,
                    {"family-east","family-west"},"real-region","home",
                    revision,owner,true);
        };
        auto result=good(1,1);
        check(result.registered==6&&result.living_named==6&&result.dependents==0&&
              result.named_present_for_duty==6&&result.named_away_or_transit==0,
              "two distinct family scopes did not reconcile six real staff");
        refuse([&]{(void)verify_regional_population(w,path,ship,
            {"family-east","family-east"},"real-region","home",1,1);},
            "duplicated family scope counted twice");
        // A second family scope claiming an already enrolled named NPC is not a
        // valid multi-family production region, even if its population matches.
        {
            NpcAppearanceStore duplicated(path,"duplicate-person",9919,[&](auto id){
                return w.world().npc_records().count(id)!=0;});
            duplicated.register_settlement(ship,1);
            duplicated.enroll_family_founder(1,ship);
        }
        refuse([&]{(void)verify_regional_population(w,path,ship,
            {"family-east","family-west","duplicate-person"},"real-region","home",1,1);},
            "same living named NPC enrolled in two family registries");
        // A lookalike ECON03 checkpoint with the wrong regional population
        // cannot be used to justify a production owner handoff.
        re::CheckpointStore bad_region(path,"wrong-headcount");
        re::Scheduler wrong;cold_colony::State shortfall{};shortfall.population=5;
        wrong.add_colony("home",shortfall);
        check(bad_region.save(wrong,0)==1,"bad regional fixture unavailable");
        re::RegionalModeAuthority bad_auth(bad_region);
        check(bad_auth.enroll().generation==1,"bad region owner setup failed");
        refuse([&]{(void)verify_regional_population(w,path,ship,
            {"family-east","family-west"},"wrong-headcount","home",1,1);},
            "stale/understated regional population passed handoff preflight");
        // An ownership phase is NOT a completed handoff. Reject during
        // preparation and permit only after the durable owner CAS succeeds.
        auto state=auth.state();
        auto prepared=auth.prepare("warm-stage",re::SimulationOwner::Detailed,
            state.generation,state.checkpoint_revision,state.boundary_tick);
        refuse([&]{(void)good(1,prepared.generation);},
            "prepared cold/warm owner phase passed production census audit");
        auto abort=auth.abort("warm-stage",prepared.generation);
        result=good(1,abort.generation);
        check(result.registered==6,"aborted warm handoff rewrote people");
        prepared=auth.prepare("warm-commit",re::SimulationOwner::Detailed,
            abort.generation,abort.checkpoint_revision,abort.boundary_tick);
        auto warm=auth.commit("warm-commit",prepared.generation,
            prepared.checkpoint_revision,[](sqlite3*,re::Scheduler&){});
        result=good(warm.checkpoint_revision,warm.generation);
        check(result.registered==6,"cold/detailed owner change duplicated residents");
        prepared=auth.prepare("cold-commit",re::SimulationOwner::Cold,
            warm.generation,warm.checkpoint_revision,warm.boundary_tick);
        auto cold=auth.commit("cold-commit",prepared.generation,
            prepared.checkpoint_revision,[](sqlite3*,re::Scheduler&){});
        result=good(cold.checkpoint_revision,cold.generation);
        check(result.named_present_for_duty==6&&result.registered==6,
            "detailed/cold owner return lost production family census");
        {PersistentWorld restarted(path,Simulation(9919));
         const auto proof=verify_regional_population(restarted,path,ship,
              {"family-east","family-west"},"real-region","home",
              cold.checkpoint_revision,cold.generation,true);
         check(proof.registered==6,"regional population ownership did not survive restart");}
        std::cout<<"PASS v49 shared-scope regional population and cold/detailed owner preflight"<<'\n';
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL v49 region: "<<e.what()<<'\n';return 1;
    }
}
