#include "galaxy/Persistence.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <stdexcept>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("PLAY13C5: ")+#x+" line "+std::to_string(__LINE__));}while(false)
static bool rejects(const std::string& data) {
    try{(void)Simulation::from_snapshot(data);return false;}
    catch(const std::exception&){return true;}
}
int main(){
    const auto dir=fs::temp_directory_path()/
      ("sg_play13c5_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try{
        fs::create_directories(dir);
        const auto db=(dir/"world.sqlite").string();
        EntityId ship=0,site=0;
        std::uint64_t body_a=0,body_b=0;
        std::int64_t initial_mass=0;
        std::string after_mining;
        {
            Simulation genesis(13505);
            ship=genesis.add_colony_ship(7,{0,0},60,12);
            genesis.provision_starter_exploration_scanner(ship);
            genesis.add_asteroid({30000,0},100);
            initial_mass=genesis.total_material_kg();
            PersistentWorld world(db,std::move(genesis));
            world.ensure_physical_celestial_systems();
            world.ensure_ship_frame_registry();
            const auto& bodies=world.world().physical_celestial_systems().at(1);
            body_a=bodies.front().id;
            for(const auto& body:bodies)
                if(body.kind==CelestialKind::Planet){body_b=body.id;break;}
            CHECK(body_a && body_b && body_a!=body_b);
            CHECK(world.begin_celestial_transit(7,ship,body_a,4001));
            world.steps(4);
            CHECK(world.complete_celestial_transit(4001));
            CHECK(world.world().canonical_state().rfind("galaxy-kernel-v74|",0)==0);
            CHECK(world.world().local_environments().count(body_a)==1);
            const auto profile=world.world().local_environments().at(body_a);
            CHECK(profile.zone==1 && profile.hazard==3);
            CHECK(profile.first!=profile.second);
            const auto manifest=world.world().generated_local_regions().at(body_a);
            CHECK(manifest[0] && manifest[1]);
            site=manifest[0];
            CHECK(world.world().local_deposit_taxonomy(manifest[0])==profile.first);
            CHECK(world.world().local_deposit_taxonomy(manifest[1])==profile.second);
            CHECK(world.world().total_material_kg()==initial_mass);
            const auto before=world.world().canonical_state();
            // Profile is deterministic from catalogue, not a mutable client guess.
            auto tampered=before;
            const auto at=tampered.find("localenvironment|");
            CHECK(at!=std::string::npos);
            const auto end=tampered.find('\n',at);
            CHECK(end!=std::string::npos);
            const auto original=tampered.substr(at,end-at);
            CHECK(original.find("|3|")!=std::string::npos);
            const auto hazard=original.find("|3|");
            auto altered=original;altered.replace(hazard,3,"|0|");
            tampered.replace(at,end-at,altered);
            CHECK(rejects(tampered));
            // A missing environmental profile cannot silently downgrade a v74 world.
            auto missing=before;
            missing.erase(at,end-at+1);
            CHECK(rejects(missing));
            // v73 compatibility is explicit and upgrade-only, not regenerated ore.
            auto older=before;
            older.replace(0,17,"galaxy-kernel-v73");
            const auto legacy=older.find("localenvironment|");
            const auto legacy_end=older.find('\n',legacy);
            CHECK(legacy!=std::string::npos && legacy_end!=std::string::npos);
            older.erase(legacy,legacy_end-legacy+1);
            auto migrated=Simulation::from_snapshot(older);
            CHECK(migrated.local_environments().empty());
            const auto preserved_mass=migrated.total_material_kg();
            migrated.ensure_local_environment_profiles();
            CHECK(migrated.canonical_state()==before);
            CHECK(migrated.total_material_kg()==preserved_mass);
            CHECK(world.world().coarse_probe_signatures(7,ship).size()==2);
            Command mine{};
            mine.id=5001;mine.sequence=5001;
            mine.execute_tick=world.world().tick()+1;
            mine.issuer=7;mine.actor=ship;mine.target=site;
            mine.action=Action::Mine;mine.amount=1;
            CHECK(world.submit(mine));world.step();
            CHECK(world.world().events().back().result==Outcome::Applied);
            CHECK(world.world().events().back().transferred==1);
            CHECK(world.world().entities().at(site).ore==5);
            const auto& minerals=world.world().colony_ship(ship)->minerals_kg;
            std::int64_t separated=0;
            for(const auto& [name,kg]:minerals){(void)name;separated+=kg;}
            CHECK(separated==1000); // one tonne partitioned in existing kg ledger
            CHECK(world.world().find(ship)->cargo==0);
            CHECK(world.world().total_material_kg()==initial_mass);
            after_mining=world.world().canonical_state();
        }
        {
            PersistentWorld world(db,Simulation(55));
            CHECK(world.world().canonical_state()==after_mining);
            CHECK(world.world().local_deposit_taxonomy(site).has_value());
            CHECK(world.world().colony_ship(ship)->crew==12);
            CHECK(world.world().total_material_kg()==initial_mass);
            CHECK(world.begin_celestial_transit(7,ship,body_b,4002));
            world.steps(4);CHECK(world.complete_celestial_transit(4002));
            const auto b=world.world().local_environments().at(body_b);
            CHECK(b.zone==3 || b.zone==4);
            CHECK(b.zone!=world.world().local_environments().at(body_a).zone);
            CHECK(b.first!=b.second);
            CHECK(world.world().total_material_kg()==initial_mass);
            world.ensure_local_environment_profiles();
            CHECK(world.world().total_material_kg()==initial_mass);
        }
        fs::remove_all(dir);
        std::cout<<"PASS PLAY13C5 deterministic environments, real mineral recovery, v73 migration, tamper rejection\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL PLAY13C5 "<<e.what()<<'\n';
        std::error_code ec;fs::remove_all(dir,ec);return 1;
    }
}
