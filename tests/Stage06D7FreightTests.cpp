// S6-D7 original v136 material/transport schema acceptance, using an actual
// constructed native site and shuttle from the original S6-F5E producer.
// Never fabricate positive cargo or forge source-extraction receipts.
#include "galaxy/Stage6ConsolidatedAdmission.hpp"
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(\
    "S6D7 line "+std::to_string(__LINE__)+": " #x);}while(false)
static bool rejects(const std::function<void()>& action){
    try{action();return false;}catch(const std::exception&){return true;}
}
int main(int argc,char** argv){
    if(argc!=3)return 2;
    try {
        const std::string db=argv[1],reopen=argv[2];
        if(!fs::exists(fs::u8path(db)))throw std::runtime_error("missing original fixture");
        PersistentWorld world(db,Simulation(0xD7));
        const auto before=stage6::consolidated::admit(world);
        CHECK(before.shuttles>=1 && before.sites>=1 && before.atmosphere_v135);
        const auto& shuttles=world.world().planetary_shuttles();
        const auto key=shuttles.begin()->first;
        const auto ship=shuttles.begin()->second.source_ship;
        const auto site=world.world().planetary_sites().at(key);
        const auto owner=shuttles.begin()->second.owner;
        CHECK(shuttles.begin()->second.flight_phase==0);
        CHECK(site.owner==owner && site.source_ship==ship);
        const auto generation=world.generation();
        const auto hash=world.world().state_hash();
        const auto mass=world.world().total_ore();
        CHECK(!world.load_planetary_geology_shuttle(
            owner+1,ship,key,PlanetaryResource::Soil,1,909001));
        CHECK(!world.load_planetary_geology_shuttle(
            owner,ship,key,PlanetaryResource::Water,1,909002));
        CHECK(!world.load_planetary_geology_shuttle(
            owner,ship,key,PlanetaryResource::Soil,1001,909003));
        CHECK(!world.load_planetary_geology_shuttle(
            owner,ship,key,PlanetaryResource::Soil,1,909004));
        CHECK(!world.unload_planetary_geology_shuttle(
            owner,ship,key,PlanetaryResource::Soil,1,909005));
        CHECK(world.generation()==generation);
        CHECK(world.world().state_hash()==hash);
        CHECK(world.world().total_ore()==mass);
        // An explicit zero-cargo schema upgrade is TEST-ONLY and creates no
        // shipment, receipt, resource, extra crew or apparent landing.
        auto candidate=world.world().canonical_state();
        CHECK(candidate.rfind("galaxy-kernel-v135|",0)==0);
        candidate.replace(0,std::string("galaxy-kernel-v135").size(),
                          "galaxy-kernel-v136");
        const auto p=candidate.find("planetshuttle|");
        CHECK(p!=std::string::npos);
        const auto end=candidate.find('\n',p);
        CHECK(end!=std::string::npos);
        candidate.insert(end,"|0|0|0");
        const auto migrated=Simulation::from_snapshot(candidate);
        CHECK(migrated.canonical_state()==candidate);
        CHECK(migrated.total_ore()==mass);
        CHECK(migrated.planetary_shuttles().at(key).regional_geology_kg[0]==0);
        const auto hold=std::string("|0|0|0");
        auto missing=candidate;
        missing.erase(end,hold.size());
        CHECK(rejects([&]{(void)Simulation::from_snapshot(missing);}));
        // Cargo that was never debited from a physical source must fail
        // original world mass verification. A fake manifest is NOT freight.
        auto forged=candidate;
        forged.replace(end,hold.size(),"|100|0|0");
        CHECK(rejects([&]{(void)Simulation::from_snapshot(forged);}));
        for(const char* suffix:{"","-wal","-shm"}) {
            std::error_code ec;fs::remove(reopen+suffix,ec);
        }
        {
            PersistentWorld version136(reopen,Simulation::from_snapshot(candidate));
            CHECK(version136.world().total_ore()==mass);
            CHECK(version136.world().canonical_state()==candidate);
        }
        {
            PersistentWorld recovered(reopen,Simulation(0xD7));
            CHECK(recovered.world().canonical_state()==candidate);
            CHECK(recovered.world().total_ore()==mass);
        }
        std::cout<<"S6-D7 original geological shuttle schema/denial/SQLite restart PASS\n";
        return 0;
    } catch(const std::exception& e){
        std::cerr<<"S6-D7 native regression FAILED: "<<e.what()<<'\n';
        return 1;
    }
}
