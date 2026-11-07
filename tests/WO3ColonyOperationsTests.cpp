#include "galaxy/WO3ColonyOperations.hpp"
#include "galaxy/Persistence.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error( \
    "WO3 native line "+std::to_string(__LINE__)+": " #x);}while(0)

static Event commit(PersistentWorld& w,EntityId ship,std::uint64_t id,
                    Action act,EntityId site=0,std::int64_t amount=1,
                    int opt=0,PlayerId owner=7) {
    Command c{};
    c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
    c.issuer=owner;c.actor=ship;c.target=site;c.action=act;
    c.amount=amount;c.thrust_x=opt;
    CHECK(w.submit(c));
    w.step();
    CHECK(w.world().events().back().command_id==id);
    return w.world().events().back();
}
static void expect_applied(PersistentWorld& w,EntityId ship,std::uint64_t id,
                           Action act,EntityId target=0,
                           std::int64_t amount=1,int opt=0) {
    CHECK(commit(w,ship,id,act,target,amount,opt).result==Outcome::Applied);
}
static void construct(PersistentWorld& w,EntityId ship,EntityId site,
                      colony03::Facility kind,std::uint64_t command_id) {
    const auto original_modules=w.world().colony_ship(ship)->modules;
    const auto original_mass=w.world().total_material_kg();
    const auto plan=colony03::plan_facility(w.world(),7,ship,site,kind);
    CHECK(plan.gate==colony03::ConstructionGate::Ready);
    CHECK(!colony03::approved_original_facility(
        w.world(),7,ship,site,kind,command_id,false));
    const auto hash=w.world().state_hash();
    auto cmd=colony03::approved_original_facility(
        w.world(),7,ship,site,kind,command_id,true);
    CHECK(cmd.has_value());
    CHECK(w.world().state_hash()==hash); // proposal never reserves stock
    CHECK(cmd->issuer==7 && cmd->actor==ship && cmd->target==site &&
          cmd->action==plan.original_action && cmd->amount==1);
    CHECK(w.submit(*cmd));
    CHECK(!w.submit(*cmd));
    w.step();
    CHECK(w.world().events().back().command_id==command_id &&
          w.world().events().back().result==Outcome::Applied);
    CHECK(w.world().colony_ship(ship)->modules==original_modules-plan.modules);
    CHECK(colony03::installed(w.world(),site,kind));
    CHECK(w.world().total_material_kg()==original_mass);
    CHECK(colony03::plan_facility(w.world(),7,ship,site,kind).gate==
          colony03::ConstructionGate::AlreadyBuilt);
    CHECK(!colony03::approved_original_facility(
        w.world(),7,ship,site,kind,command_id+500,true));
}
int main() {
 try{
    CHECK(colony03::kNativeFacilities.size()==10);
    const auto folder=fs::temp_directory_path()/
      ("wo3_colony_"+std::to_string(std::chrono::steady_clock::now().
                                  time_since_epoch().count()));
    fs::create_directories(folder);
    const auto db=(folder/"world.sqlite").string();
    EntityId ship=0,site=0;
    std::int64_t initial_mass=0;
    {
        Simulation genesis(93003);
        ship=genesis.add_colony_ship(7,{0,0},200,8);
        genesis.provision_colony_genesis(ship,1000,3000,1000,1000,1000);
        const auto rock=genesis.add_asteroid({2000,0},120);
        genesis.enable_individual_workforce();
        initial_mass=genesis.total_material_kg();
        PersistentWorld w(db,std::move(genesis));
        CHECK(!colony03::audit_original_outpost(w.world(),8,ship,1));
        CHECK(!colony03::audit_original_outpost(w.world(),7,ship,999999));
        CHECK(!colony03::approved_original_facility(w.world(),7,ship,1,
             colony03::Facility::Habitat,1,true));
        expect_applied(w,ship,1,Action::Mine,rock,80);
        expect_applied(w,ship,2,Action::RefineOre,0,80);
        expect_applied(w,ship,3,Action::FabricateModule,0,25);
        expect_applied(w,ship,4,Action::ConstructOutpost);
        CHECK(w.world().built_outposts().size()==1);
        site=w.world().built_outposts().begin()->first;
        const auto a=colony03::audit_original_outpost(w.world(),7,ship,site);
        CHECK(a.has_value());
        CHECK(!a->local_housing_installed && a->housed_residents==0);
        CHECK(a->physically_present_named==0 && a->modules_aboard>0);
        CHECK(!colony03::audit_original_outpost(w.world(),8,ship,site));
        CHECK(colony03::plan_facility(w.world(),8,ship,site,
              colony03::Facility::Habitat).gate==
              colony03::ConstructionGate::InvalidOwner);
        CHECK(colony03::plan_facility(w.world(),7,ship,999999,
              colony03::Facility::Habitat).gate==
              colony03::ConstructionGate::MissingOriginalOutpost);
        CHECK(colony03::plan_facility(w.world(),7,ship,site,
              colony03::Facility::Community).gate==
              colony03::ConstructionGate::MissingHabitat);
        CHECK(colony03::plan_facility(w.world(),7,ship,site,
              colony03::Facility::Waterworks).gate==
              colony03::ConstructionGate::MissingHabitat);
        CHECK(colony03::plan_facility(w.world(),7,ship,site,
              colony03::Facility::FrameFinisher).gate==
              colony03::ConstructionGate::MissingWorkshop);
        // Actual physical construction uses original owner commands, consumes
        // finite fabricated modules, embeds exactly their tonnage and survives
        // original immutable SQLite transaction/recovery.
        construct(w,ship,site,colony03::Facility::Habitat,10);
        CHECK(colony03::plan_facility(w.world(),7,ship,site,
              colony03::Facility::Waterworks).gate==
              colony03::ConstructionGate::MissingUtility);
        construct(w,ship,site,colony03::Facility::Utility,11);
        construct(w,ship,site,colony03::Facility::Waterworks,12);
        construct(w,ship,site,colony03::Facility::Reclaimer,13);
        construct(w,ship,site,colony03::Facility::Farm,14);
        construct(w,ship,site,colony03::Facility::Community,15);
        construct(w,ship,site,colony03::Facility::Clinic,16);
        construct(w,ship,site,colony03::Facility::Workshop,17);
        construct(w,ship,site,colony03::Facility::FrameFinisher,18);
        CHECK(w.world().total_material_kg()==initial_mass);
        // Real residents require physical delivered provisions, not the
        // existence of a housing catalog item or invented NPCs.
        for(int n=1;n<=3;++n)
            expect_applied(w,ship,19+n,Action::TransferHabitatStock,site,100,n);
        expect_applied(w,ship,24,Action::AssignHabitatResidents,site,2);
        const auto housed=colony03::audit_original_outpost(w.world(),7,ship,site);
        CHECK(housed && housed->housed_residents==2);
        CHECK(housed->aggregate_habitat_headcount_matches_named);
        CHECK(housed->physically_present_named>=2 &&
              housed->water_kg>0 && housed->food_kg>0 && housed->oxygen_kg>0);
        CHECK(housed->housing_capacity>=housed->housed_residents);
        CHECK(!housed->aggregate_farm_headcount_matches_named ||
              housed->local_farm_workers==0);
        const auto outpost_hash=w.world().state_hash();
        const auto plans=colony03::plan_native_facilities(w.world(),7,ship,site);
        CHECK(plans.size()==10 && w.world().state_hash()==outpost_hash);
        CHECK(plans[9].facility==colony03::Facility::Storage &&
              plans[9].can_request());
        // Save an original accepted-but-pending construction before effect.
        auto pending=colony03::approved_original_facility(
            w.world(),7,ship,site,colony03::Facility::Storage,25,true);
        CHECK(pending && w.submit(*pending));
        CHECK(!colony03::approved_original_facility(
            w.world(),7,ship,site,colony03::Facility::Storage,25,true));
        const auto pending_copy=(folder/"pending_copy.sqlite").string();
        PersistentWorld::recover_copy(db,pending_copy);
        {
            PersistentWorld copied(pending_copy,Simulation(44));
            CHECK(!colony03::installed(copied.world(),site,colony03::Facility::Storage));
            CHECK(copied.world().state_hash()==w.world().state_hash());
            copied.step();
            w.step();
            CHECK(copied.world().events().back().result==Outcome::Applied);
            CHECK(copied.world().state_hash()==w.world().state_hash());
            CHECK(copied.world().total_material_kg()==initial_mass);
        }
        const auto finished_copy=(folder/"finished_copy.sqlite").string();
        PersistentWorld::recover_copy(db,finished_copy);
        PersistentWorld finished(finished_copy,Simulation(66));
        CHECK(finished.world().state_hash()==w.world().state_hash());
        CHECK(finished.world().total_material_kg()==initial_mass);
        CHECK(colony03::installed(finished.world(),site,colony03::Facility::Storage));
        const auto restored=colony03::audit_original_outpost(
            finished.world(),7,ship,site);
        CHECK(restored && restored->housed_residents==housed->housed_residents &&
              restored->physically_present_named==housed->physically_present_named &&
              restored->aggregate_habitat_headcount_matches_named);
        // The orbital colony/planetary surface never shares a fake station ID.
        CHECK(!colony03::audit_planetary_surface(
            finished.world(),7,ship,PlanetarySiteKey{}));
        CHECK(!colony03::audit_planetary_surface(
            finished.world(),8,ship,PlanetarySiteKey{}));
    }
    fs::remove_all(folder);
    std::cout<<"WO3 original construction, finite NPC census and SQLite replay OK\n";
    return 0;
 }catch(const std::exception& e){
    std::cerr<<e.what()<<"\n";return 1;
 }
}
