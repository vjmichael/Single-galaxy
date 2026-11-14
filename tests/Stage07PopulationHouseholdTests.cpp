#include "galaxy/Stage07PopulationHouseholds.hpp"
#include "galaxy/Persistence.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace galaxy;
namespace fs=std::filesystem;
static void check(bool valid,const char* reason) {
    if(!valid)throw std::runtime_error(reason);
}
template<class F>static void deny(F&& action,const char* reason) {
    bool rejected=false;
    try{action();}catch(const std::runtime_error&){rejected=true;}
    check(rejected,reason);
}
static void integrity(const std::string& path) {
    sqlite3* db=nullptr;
    check(sqlite3_open_v2(path.c_str(),&db,SQLITE_OPEN_READONLY,nullptr)==SQLITE_OK,
          "SQLite read-only open failed");
    sqlite3_stmt* query=nullptr;
    const int prepared=sqlite3_prepare_v2(db,"PRAGMA integrity_check",-1,&query,nullptr);
    check(prepared==SQLITE_OK,"SQLite integrity query failed");
    const bool sound=sqlite3_step(query)==SQLITE_ROW &&
          std::string(reinterpret_cast<const char*>(sqlite3_column_text(query,0)))=="ok";
    sqlite3_finalize(query);
    sqlite3_close(db);
    check(sound,"SQLite original world integrity failed");
}
int main() {
    const auto root=fs::temp_directory_path()/
       ("sg_stage07_pop01_"+std::to_string(std::chrono::steady_clock::now().
         time_since_epoch().count()));
    const auto original=(root/"original.sqlite").string();
    const auto replica=(root/"independent.sqlite").string();
    try {
        fs::create_directories(root);
        Simulation seed(17007);
        const auto ship=seed.add_colony_ship(7,{0,0},100,6);
        seed.enable_individual_workforce();
        const auto initial=stage7::project(seed);
        check(initial.named_living==6 && initial.registered_dependents==0 &&
              initial.casualties==0 && initial.all_living_people()==6 &&
              initial.active_households==0,
              "canonical named genesis was not observed exactly");
        const transport::Endpoint legal_home{
            transport::LocationKind::ColonyShip,ship};
        check(initial.named_by_legal_home.at(legal_home)==6 &&
              initial.physically_present.at(ship)==6 &&
              initial.named_away_from_legal_home==0,
              "legal address and physical ship presence were conflated");

        // Pure audit-input exercises cannot mutate canonical world custody.
        auto persons=seed.npc_records();
        auto children=seed.physical_dependents();
        auto casualties=seed.npc_casualties();
        auto sites=seed.planetary_sites();
        const auto child_id=(1ULL<<62)+17007;
        PhysicalDependent child{};
        child.child_id=child_id;child.guardian_id=1;child.home_ship=ship;
        child.location=ship;child.registration_command=17009;child.registered_tick=1;
        children.emplace(child_id,child);
        auto preview=stage7::project_records(persons,children,casualties,sites);
        check(preview.named_living==6 && preview.registered_dependents==1 &&
              preview.all_living_people()==7 && preview.active_households==0,
              "one dependent was doubled or admitted without housing");
        PlanetarySiteFacility site{};
        site.source_ship=ship;site.surface_location=17012;
        PlanetaryHouseholdResidence family{};
        family.co_parent_id=2;family.child_ids={child_id};
        family.booking_request=17013;family.arrival_receipt=17014;
        family.admission_request=17015;family.admission_tick=2;family.consent_mask=3;
        site.households.emplace(1,family);
        const PlanetarySiteKey first{17010,17,36,1};
        sites.emplace(first,site);
        preview=stage7::project_records(persons,children,casualties,sites);
        check(preview.active_households==1 && preview.all_living_people()==7 &&
              preview.households.at({first,1}).admitted &&
              preview.households.at({first,1}).child_ids.size()==1,
              "two-parent admission duplicated original dependent identity");
        // An already admitted child cannot also reside in a second household.
        auto duplicate=sites;
        const PlanetarySiteKey second{17011,17,36,1};
        duplicate.emplace(second,site);
        deny([&]{(void)stage7::project_records(persons,children,casualties,duplicate);},
             "same child admitted twice");
        duplicate=sites;
        duplicate.at(first).households.at(1).consent_mask=1;
        deny([&]{(void)stage7::project_records(persons,children,casualties,duplicate);},
             "one-parent authorization accepted as two-parent admission");
        auto missing=children;missing.clear();
        deny([&]{(void)stage7::project_records(persons,missing,casualties,sites);},
             "nonexistent dependent accepted as real member");
        auto clash=persons;clash.at(1).id=2;
        deny([&]{(void)stage7::project_records(clash,{},casualties,{});},
             "mismatched authoritative NPC identity accepted");

        // Return is immutable history; it does not add a second household.
        sites.at(first).households.at(1).return_request=17016;
        sites.at(first).households.at(1).return_tick=3;
        const auto home=stage7::project_records(persons,children,casualties,sites);
        check(home.active_households==0 && home.historical_households==1 &&
              home.all_living_people()==7 &&
              home.households.at({first,1}).returned,
              "returned household incorrectly counted as active");

        // Independent SQLite copy/restart must reconstruct the exact same
        // living people from the committed original Simulation, not preview.
        {
            PersistentWorld w(original,std::move(seed));
            w.steps(3);
            integrity(original);
            PersistentWorld::recover_copy(original,replica);
            integrity(replica);
            PersistentWorld reopened(replica,Simulation(11));
            check(reopened.world().canonical_state()==w.world().canonical_state(),
                  "original SQLite recovered different canonical population");
            const auto a=stage7::project(w.world());
            const auto b=stage7::project(reopened.world());
            check(a.named_living==6 && b.named_living==6 &&
                  a.all_living_people()==b.all_living_people() &&
                  a.named_by_legal_home==b.named_by_legal_home &&
                  a.physically_present==b.physically_present &&
                  a.active_households==b.active_households,
                  "Stage 7 census diverged across independent restart");
        }
        std::error_code ignored;fs::remove_all(root,ignored);
        std::cout<<"PASS Stage7 POP-01 original NPC and registered-family census, "
                    "duplicate/consent denial, SQLite integrity, independent restart\n";
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"FAIL Stage7 POP-01 "<<e.what()<<'\n';
        std::error_code ignored;fs::remove_all(root,ignored);
        return 1;
    }
}
