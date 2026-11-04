#include "galaxy/Persistence.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("PLAY13D3: ")+#x+" line "+std::to_string(__LINE__));}while(false)
struct Chosen {std::uint32_t system=0;std::uint64_t body=0;std::uint16_t latitude=0,longitude=0;};
static Chosen select_site(bool farm) {
    for(std::uint32_t system=1;system<=2000;++system) {
        Simulation sample(613,system);
        const auto ship=sample.add_colony_ship(7,{0,0},60,8);
        sample.provision_starter_exploration_scanner(ship);
        sample.ensure_physical_celestial_systems();
        for(const auto& body:sample.physical_celestial_systems().at(system)) {
            if(body.kind!=CelestialKind::Planet && body.kind!=CelestialKind::Moon)continue;
            if(farm && (body.orbital_radius_milli_au<2200 ||
                        body.orbital_radius_milli_au>12000))continue;
            if(!farm) {
                if(!sample.celestial_oasis_prospect(body.id)->candidate)continue;
                const auto source=sample.planetary_oasis_source_region(7,ship,body.id);
                if(source)return {system,body.id,source->latitude,source->longitude};
                continue;
            }
            for(const auto [y,x]:std::vector<std::pair<unsigned,unsigned>>{
                {17,36},{17,18},{18,46},{16,60},{14,4},{20,32},{10,27},{26,52},
                {7,15},{28,8},{12,40},{24,55},{19,50},{15,64}}) {
                const auto region=sample.scan_planetary_geography(7,ship,body.id,
                    std::uint16_t(y),std::uint16_t(x));
                if(region && !region->landscape.region.surface_water &&
                   (!farm || region->landscape.region.farming_possible))
                    return {system,body.id,std::uint16_t(y),std::uint16_t(x)};
            }
        }
    }
    throw std::runtime_error("no suitable physically catalogued planetary region");
}
static void fabricate(PersistentWorld& w,EntityId ship) {
    auto apply=[&](std::uint64_t id,Action action,std::int64_t amount,EntityId target=0){
        Command c{};c.id=id;c.sequence=id;c.execute_tick=w.world().tick()+1;
        c.issuer=7;c.actor=ship;c.action=action;c.amount=amount;c.target=target;
        CHECK(w.submit(c));w.step();
        CHECK(w.world().events().back().result==Outcome::Applied);
    };
    apply(2001,Action::Mine,8,ship+1);
    apply(2002,Action::RefineOre,8);
    apply(2003,Action::FabricateModule,4);
    CHECK(w.world().colony_ship(ship)->modules>=4);
}
static void scenario(const fs::path& db, bool farming){
    const auto chosen=select_site(farming);
    EntityId ship=0;
    std::int64_t origin=0;
    std::string durable;
    {
        Simulation seed(613,chosen.system);
        ship=seed.add_colony_ship(7,{0,0},60,8);
        seed.provision_colony_genesis(ship,2000,2000,2000,2000,2000);
        seed.provision_starter_exploration_scanner(ship);
        seed.add_asteroid({2000,0},60);
        seed.ensure_physical_celestial_systems();
        if(!farming)seed.provision_oasis_reserve_genesis(chosen.body,8000,4000);
        seed.ensure_ship_frame_registry();
        origin=seed.total_ore();
        PersistentWorld w(db.string(),std::move(seed));
        fabricate(w,ship);
        CHECK(w.begin_celestial_transit(7,ship,chosen.body,90001));
        w.steps(4);
        CHECK(w.complete_celestial_transit(90001));
        CHECK(w.world().total_ore()==origin);
        CHECK(!w.build_planetary_site(8,ship,chosen.body,chosen.latitude,chosen.longitude));
        auto pre=w.world().canonical_state();
        bool failed=false;
        try{(void)w.build_planetary_site(7,ship,chosen.body,
            chosen.latitude,chosen.longitude,FailPoint::AfterSnapshotWrite);}
        catch(const std::exception&){failed=true;}
        CHECK(failed);
        CHECK(w.world().canonical_state()==pre);
        CHECK(w.build_planetary_site(7,ship,chosen.body,chosen.latitude,chosen.longitude));
        CHECK(!w.build_planetary_site(7,ship,chosen.body,chosen.latitude,chosen.longitude));
        CHECK(w.equip_planetary_site(7,ship,chosen.body,chosen.latitude,chosen.longitude,farming));
        CHECK(!w.equip_planetary_site(7,ship,chosen.body,chosen.latitude,chosen.longitude,farming));
        CHECK(w.world().colony_ship(ship)->modules==0);
        if(!farming) {
            const auto source=w.world().planetary_oasis_source_region(7,ship,chosen.body);
            CHECK(source && source->latitude==chosen.latitude &&
                  source->longitude==chosen.longitude);
        }
        if(farming) {
            CHECK(w.supply_planetary_site(7,ship,chosen.body,chosen.latitude,chosen.longitude,true,100));
            CHECK(w.supply_planetary_site(7,ship,chosen.body,chosen.latitude,chosen.longitude,false,50));
            CHECK(!w.cultivate_planetary_site(8,ship,chosen.body,chosen.latitude,chosen.longitude,2));
            CHECK(w.cultivate_planetary_site(7,ship,chosen.body,chosen.latitude,chosen.longitude,2));
        }else{
            CHECK(!w.extract_planetary_site(7,ship,chosen.body,chosen.latitude,chosen.longitude,true,9000));
            CHECK(w.extract_planetary_site(7,ship,chosen.body,chosen.latitude,chosen.longitude,true,1000));
            CHECK(w.world().oasis_reserves().at(chosen.body).water_kg==7000);
        }
        const auto key=PlanetarySiteKey{chosen.body,chosen.latitude,chosen.longitude,1};
        const auto& region=w.world().planetary_sites().at(key);
        CHECK(region.owner==7&&region.source_ship==ship&&region.embodied_ore==8);
        if(farming) {
            CHECK(region.food_kg==24 && region.wastewater_kg==6);
            CHECK(w.retrieve_planetary_site(7,ship,chosen.body,chosen.latitude,chosen.longitude,true,24));
        } else {
            CHECK(region.water_kg==1000);
            CHECK(w.retrieve_planetary_site(7,ship,chosen.body,chosen.latitude,chosen.longitude,false,1000));
            CHECK(w.world().oasis_reserves().at(chosen.body).water_kg==7000);
        }
        CHECK(w.world().total_ore()==origin);
        durable=w.world().canonical_state();
        CHECK(durable.rfind("galaxy-kernel-v77|",0)==0);
        CHECK(Simulation::from_snapshot(durable).canonical_state()==durable);
    }
    {
        PersistentWorld w(db.string(),Simulation(2));
        CHECK(w.world().canonical_state()==durable);
        CHECK(w.world().total_ore()==origin);
        CHECK(w.world().planetary_sites().size()==1);
        CHECK(!w.retrieve_planetary_site(8,ship,chosen.body,chosen.latitude,chosen.longitude,false,1));
        // No material or facility exists at an arbitrary neighboring tile.
        CHECK(!w.supply_planetary_site(7,ship,chosen.body,
            chosen.latitude,std::uint16_t((chosen.longitude+1)%72),true,1));
    }
}
int main(){
    const auto dir=fs::temp_directory_path()/
      ("sg_play13d3_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try{
        fs::create_directories(dir);
        scenario(dir/"farm.sqlite",true);
        scenario(dir/"extract.sqlite",false);
        fs::remove_all(dir);
        std::cout<<"PASS PLAY13D3 regional farm and finite extraction, physical custody, mass, rollback, SQLite restart\n";
    }catch(const std::exception& e){
        std::error_code ec;fs::remove_all(dir,ec);
        std::cerr<<"FAIL PLAY13D3 "<<e.what()<<'\n';
        return 1;
    }
}
