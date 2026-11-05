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
    // Use the existing verified 4-ore/2-module production cadence.
    apply(2001,Action::Mine,4,ship+1);
    apply(2002,Action::RefineOre,4);
    apply(2003,Action::FabricateModule,2);
    apply(2004,Action::Mine,4,ship+1);
    apply(2005,Action::RefineOre,4);
    apply(2006,Action::FabricateModule,2);
    // Third batch is fabricated before departure: the asteroid stays in
    // the original celestial frame and cannot be mined remotely from orbit.
    apply(2007,Action::Mine,4,ship+1);
    apply(2008,Action::RefineOre,4);
    apply(2009,Action::FabricateModule,2);
    CHECK(w.world().colony_ship(ship)->modules>=6);
}
static void scenario(const fs::path& db, bool farming){
    const auto chosen=select_site(farming);
    EntityId ship=0;
    std::uint64_t first_npc=0,second_npc=0;
    std::int64_t origin=0;
    std::string durable;
    {
        Simulation seed(613,chosen.system);
        ship=seed.add_colony_ship(7,{0,0},60,8);
        seed.provision_colony_genesis(ship,2000,2000,2000,2000,2000);
        seed.enable_individual_workforce();
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
        std::vector<std::uint64_t> available;
        for(const auto& [id,n]:w.world().npc_records())
            if(n.home_ship==ship && n.location==ship && n.role==NpcRole::Ship)
                available.push_back(id);
        CHECK(available.size()>=3);
        first_npc=available[0];second_npc=available[1];
        CHECK(!w.build_planetary_site(7,ship,chosen.body,
            chosen.latitude,chosen.longitude,first_npc,first_npc));
        CHECK(!w.build_planetary_site(7,ship,chosen.body,
            chosen.latitude,chosen.longitude,first_npc,999999999));
        CHECK(!w.build_planetary_site(8,ship,chosen.body,chosen.latitude,chosen.longitude));
        auto pre=w.world().canonical_state();
        bool failed=false;
        try{(void)w.build_planetary_site(7,ship,chosen.body,
            chosen.latitude,chosen.longitude,FailPoint::AfterSnapshotWrite);}
        catch(const std::exception&){failed=true;}
        CHECK(failed);
        CHECK(w.world().canonical_state()==pre);
        CHECK(w.build_planetary_site(7,ship,chosen.body,chosen.latitude,chosen.longitude,
                                     first_npc,second_npc));
        CHECK(!w.build_planetary_site(7,ship,chosen.body,chosen.latitude,chosen.longitude));
        const auto key=PlanetarySiteKey{chosen.body,chosen.latitude,chosen.longitude,1};
        CHECK(!w.world().planetary_spaceport_operational(chosen.body,chosen.latitude,chosen.longitude));
        CHECK(!w.equip_planetary_site(7,ship,chosen.body,chosen.latitude,chosen.longitude,farming));
        CHECK(w.world().planetary_sites().at(key).assigned_workers==2);
        const auto& workers=w.world().planetary_sites().at(key).delegated_npcs;
        CHECK(workers[0] && workers[1] && workers[0]!=workers[1]);
        CHECK(workers[0]==first_npc && workers[1]==second_npc);
        for(const auto npc:workers){
            const auto custody=w.world().npc_custody(npc);
            CHECK(custody.home_ship==ship);
            CHECK(custody.physical_location==
                  w.world().planetary_sites().at(key).surface_location);
            CHECK(custody.assigned_role==NpcRole::PlanetarySurface);
        }
        CHECK(w.world().colony_ship(ship)->crew==6);
        w.steps(4);
        CHECK(w.world().planetary_sites().at(key).spaceport_phase==2);
        CHECK(!w.support_planetary_crew(8,ship,chosen.body,
            chosen.latitude,chosen.longitude,false,10));
        CHECK(w.support_planetary_crew(7,ship,chosen.body,
            chosen.latitude,chosen.longitude,false,10));
        CHECK(w.support_planetary_crew(7,ship,chosen.body,
            chosen.latitude,chosen.longitude,true,10));
        CHECK(w.world().total_ore()==origin);
        w.steps(239);
        CHECK(!w.world().planetary_spaceport_operational(chosen.body,chosen.latitude,chosen.longitude));
        w.step();
        CHECK(w.world().planetary_spaceport_operational(chosen.body,chosen.latitude,chosen.longitude));
        // A v78 autonomous-disabled world is still canonical and reloadable.
        CHECK(Simulation::from_snapshot(w.world().canonical_state()).canonical_state()
              ==w.world().canonical_state());
        CHECK(w.equip_planetary_site(7,ship,chosen.body,chosen.latitude,chosen.longitude,farming));
        CHECK(!w.equip_planetary_site(7,ship,chosen.body,chosen.latitude,chosen.longitude,farming));
        CHECK(w.world().colony_ship(ship)->modules==2);

        // PLAY-13D.4C.2: the extra modules were fabricated before
        // departure, not mined from an asteroid in a different local frame.
        CHECK(w.world().colony_ship(ship)->modules==2);
        CHECK(!w.build_planetary_shuttle(8,ship,key));
        const auto shuttle_before=w.world().canonical_state();
        bool shuttle_failed=false;
        try{(void)w.build_planetary_shuttle(7,ship,key,FailPoint::AfterSnapshotWrite);}
        catch(const std::exception&){shuttle_failed=true;}
        CHECK(shuttle_failed);
        CHECK(w.world().canonical_state()==shuttle_before);
        CHECK(w.build_planetary_shuttle(7,ship,key));
        CHECK(!w.build_planetary_shuttle(7,ship,key));
        CHECK(w.world().colony_ship(ship)->modules==0);
        CHECK(w.world().planetary_shuttles().at(key).dry_mass_ore==4);
        CHECK(!w.assign_planetary_shuttle_pilot(7,ship,key,first_npc));
        CHECK(!w.assign_planetary_shuttle_pilot(7,ship,key,9999999));
        CHECK(w.assign_planetary_shuttle_pilot(7,ship,key,available[2]));
        CHECK(w.world().npc_custody(available[2]).physical_location==ship);
        CHECK(!w.load_planetary_shuttle(8,ship,key,true,10));
        CHECK(!w.load_planetary_shuttle(7,ship,key,true,1001));
        const auto load_before=w.world().canonical_state();
        bool load_failed=false;
        try{(void)w.load_planetary_shuttle(7,ship,key,true,200,
                                          FailPoint::AfterSnapshotWrite);}
        catch(const std::exception&){load_failed=true;}
        CHECK(load_failed);
        CHECK(w.world().canonical_state()==load_before);
        CHECK(w.load_planetary_shuttle(7,ship,key,true,200));
        CHECK(w.load_planetary_shuttle(7,ship,key,false,100));
        CHECK(w.world().planetary_shuttles().at(key).water_kg==200);
        CHECK(w.world().planetary_shuttles().at(key).food_kg==100);
        CHECK(!w.unload_planetary_shuttle(7,ship,key,true,201));
        CHECK(w.unload_planetary_shuttle(7,ship,key,true,50));
        CHECK(w.world().planetary_shuttles().at(key).water_kg==150);
        CHECK(w.world().total_ore()==origin);
        CHECK(w.world().canonical_state().rfind("galaxy-kernel-v80|",0)==0);
        CHECK(Simulation::from_snapshot(w.world().canonical_state())
              .canonical_state()==w.world().canonical_state());

        // PLAY-13D.4C.3: the named pilot moves using the SAME NPC roster
        // as migration and ferry operations, never a shadow shuttle person.
        CHECK(!w.launch_planetary_shuttle(8,ship,key,81001,false));
        CHECK(!w.launch_planetary_shuttle(7,ship,key,0,false));
        const auto departure_before=w.world().canonical_state();
        bool departure_failed=false;
        try{(void)w.launch_planetary_shuttle(7,ship,key,81001,false,
                                             FailPoint::AfterSnapshotWrite);}
        catch(const std::exception&){departure_failed=true;}
        CHECK(departure_failed);
        CHECK(w.world().canonical_state()==departure_before);
        CHECK(w.launch_planetary_shuttle(7,ship,key,81001,false));
        CHECK(!w.launch_planetary_shuttle(7,ship,key,81001,false));
        CHECK(w.world().planetary_shuttles().at(key).flight_phase==1);
        CHECK(w.world().npc_custody(available[2]).physical_location!=ship);
        CHECK(!w.unload_planetary_shuttle(7,ship,key,true,1));
        CHECK(!w.load_planetary_shuttle(7,ship,key,false,1));
        CHECK(!w.begin_celestial_transit(7,ship,chosen.body,81002));
        CHECK(w.world().total_ore()==origin);
        const auto in_flight=w.world().canonical_state();
        CHECK(in_flight.rfind("galaxy-kernel-v81|",0)==0);
        CHECK(Simulation::from_snapshot(in_flight).canonical_state()==in_flight);
        w.steps(3);
        CHECK(w.world().planetary_shuttles().at(key).flight_phase==2);
        CHECK(w.world().planetary_shuttles().at(key).descent_receipt==81001);
        CHECK(w.world().npc_custody(available[2]).physical_location==
              w.world().planetary_sites().at(key).surface_location);
        CHECK(!w.launch_planetary_shuttle(7,ship,key,81001,true));
        CHECK(w.unload_planetary_shuttle(7,ship,key,true,60));
        CHECK(w.unload_planetary_shuttle(7,ship,key,false,20));
        CHECK(w.load_planetary_shuttle(7,ship,key,true,50));
        CHECK(w.world().total_ore()==origin);
        const auto land_snapshot=w.world().canonical_state();
        CHECK(Simulation::from_snapshot(land_snapshot).canonical_state()==land_snapshot);
        CHECK(w.launch_planetary_shuttle(7,ship,key,81003,true));
        CHECK(!w.launch_planetary_shuttle(7,ship,key,81003,true));
        CHECK(w.world().planetary_shuttles().at(key).flight_phase==3);
        CHECK(w.world().total_ore()==origin);
        CHECK(Simulation::from_snapshot(w.world().canonical_state())
              .canonical_state()==w.world().canonical_state());
        w.steps(3);
        CHECK(w.world().planetary_shuttles().at(key).flight_phase==0);
        CHECK(w.world().planetary_shuttles().at(key).ascent_receipt==81003);
        CHECK(w.world().npc_custody(available[2]).physical_location==ship);
        CHECK(!w.launch_planetary_shuttle(7,ship,key,81001,false));
        CHECK(!w.launch_planetary_shuttle(7,ship,key,81003,false));
        CHECK(w.unload_planetary_shuttle(7,ship,key,true,50));
        CHECK(w.world().total_ore()==origin);
        CHECK(Simulation::from_snapshot(w.world().canonical_state())
              .canonical_state()==w.world().canonical_state());

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
        const auto& region=w.world().planetary_sites().at(key);
        CHECK(region.owner==7&&region.source_ship==ship&&region.embodied_ore==8);
        if(farming) {
            CHECK(region.food_kg>=24 && region.wastewater_kg>=6);
            CHECK(region.harvest_cycles==2);
            CHECK(w.retrieve_planetary_site(7,ship,chosen.body,chosen.latitude,chosen.longitude,true,24));
        } else {
            CHECK(region.water_kg>=1000);
            CHECK(w.retrieve_planetary_site(7,ship,chosen.body,chosen.latitude,chosen.longitude,false,1000));
            CHECK(w.world().oasis_reserves().at(chosen.body).water_kg==7000);
        }
        CHECK(w.world().total_ore()==origin);
        // PLAY-13D.4B: the named NPC team works without repeated production
        // clicks, but cannot synthesize raw water or abandon matter custody.
        CHECK(!w.set_planetary_workforce_policy(8,chosen.body,
            chosen.latitude,chosen.longitude,1));
        CHECK(!w.set_planetary_workforce_policy(7,chosen.body,
            chosen.latitude,chosen.longitude,5));
        CHECK(!w.set_planetary_workforce_policy(7,chosen.body,
            chosen.latitude,chosen.longitude,farming?2:1));
        CHECK(w.supply_planetary_site(7,ship,chosen.body,
            chosen.latitude,chosen.longitude,true,350));
        CHECK(w.supply_planetary_site(7,ship,chosen.body,
            chosen.latitude,chosen.longitude,false,100));
        CHECK(w.support_planetary_crew(7,ship,chosen.body,
            chosen.latitude,chosen.longitude,false,150));
        CHECK(w.support_planetary_crew(7,ship,chosen.body,
            chosen.latitude,chosen.longitude,true,150));
        CHECK(w.world().total_ore()==origin);
        const auto policy=std::uint8_t(farming?1:2);
        const auto before_policy=w.world().canonical_state();
        bool policy_failed=false;
        try{(void)w.set_planetary_workforce_policy(7,chosen.body,
            chosen.latitude,chosen.longitude,policy,FailPoint::AfterSnapshotWrite);}
        catch(const std::exception&){policy_failed=true;}
        CHECK(policy_failed);
        CHECK(w.world().canonical_state()==before_policy);
        CHECK(w.set_planetary_workforce_policy(7,chosen.body,
            chosen.latitude,chosen.longitude,policy));
        CHECK(w.world().canonical_state().rfind("galaxy-kernel-v79|",0)==0);
        CHECK(Simulation::from_snapshot(w.world().canonical_state())
              .planetary_sites().at(key).workforce_policy==policy);
        const auto before_site=w.world().planetary_sites().at(key);
        const auto reserve_before=!farming?
            w.world().oasis_reserves().at(chosen.body).water_kg:0;
        w.steps(100);
        const auto after_site=w.world().planetary_sites().at(key);
        CHECK(after_site.autonomous_cycles>=before_site.autonomous_cycles+4);
        CHECK(after_site.equipment_condition<100);
        if(farming){
            CHECK(after_site.harvest_cycles>before_site.harvest_cycles);
            CHECK(after_site.nutrients_kg<before_site.nutrients_kg);
        }else{
            CHECK(after_site.auto_extracted_kg>=4000);
            CHECK(w.world().oasis_reserves().at(chosen.body).water_kg<
                  reserve_before);
            CHECK(reserve_before-w.world().oasis_reserves().at(chosen.body).water_kg==
                  std::int64_t(after_site.auto_extracted_kg));
        }
        CHECK(w.world().total_ore()==origin);
        CHECK(w.set_planetary_workforce_policy(7,chosen.body,
            chosen.latitude,chosen.longitude,4));
        const auto repairs_before=after_site.repair_cycles;
        w.steps(20);
        CHECK(w.world().planetary_sites().at(key).repair_cycles>repairs_before);
        CHECK(w.world().planetary_sites().at(key).equipment_condition==100);
        CHECK(w.world().total_ore()==origin);
        CHECK(w.set_planetary_workforce_policy(7,chosen.body,
            chosen.latitude,chosen.longitude,0));
        const auto stopped=w.world().planetary_sites().at(key).autonomous_cycles;
        w.steps(40);
        CHECK(w.world().planetary_sites().at(key).autonomous_cycles==stopped);
        CHECK(w.world().total_ore()==origin);
        // Leaving the planet must neither delete its completed structure
        // nor permit remote inventory use from another celestial frame.
        const auto& system_bodies=w.world().physical_celestial_systems().at(chosen.system);
        std::uint64_t remote=0;
        for(const auto& candidate:system_bodies)
            if(candidate.id!=chosen.body){remote=candidate.id;break;}
        CHECK(remote!=0);
        CHECK(w.begin_celestial_transit(7,ship,remote,90002));
        w.steps(4);
        CHECK(w.complete_celestial_transit(90002));
        CHECK(!w.supply_planetary_site(7,ship,chosen.body,
            chosen.latitude,chosen.longitude,true,1));
        CHECK(!w.support_planetary_crew(7,ship,chosen.body,
            chosen.latitude,chosen.longitude,true,1));
        // Administrative policy can be changed remotely, while the same
        // finite NPC workforce continues working on its original planet.
        CHECK(w.set_planetary_workforce_policy(7,chosen.body,
            chosen.latitude,chosen.longitude,policy));
        const auto remote_before=w.world().planetary_sites().at(key).autonomous_cycles;
        w.steps(40);
        CHECK(w.world().planetary_sites().at(key).autonomous_cycles>remote_before);
        if(!farming) {
            // Once the finite reserve is emptied, no further kilograms
            // appear, even while the NPC extraction policy remains active.
            CHECK(w.world().oasis_reserves().at(chosen.body).water_kg==0);
            const auto extracted=w.world().planetary_sites().at(key).auto_extracted_kg;
            const auto stalls=w.world().planetary_sites().at(key).stalled_cycles;
            w.steps(40);
            CHECK(w.world().planetary_sites().at(key).auto_extracted_kg==extracted);
            CHECK(w.world().planetary_sites().at(key).stalled_cycles>stalls);
            CHECK(w.world().oasis_reserves().at(chosen.body).water_kg==0);
            CHECK(w.world().total_ore()==origin);
        }
        CHECK(w.set_planetary_workforce_policy(7,chosen.body,
            chosen.latitude,chosen.longitude,0));
        CHECK(w.world().planetary_sites().size()==1);
        CHECK(w.world().total_ore()==origin);
        durable=w.world().canonical_state();
        CHECK(durable.rfind("galaxy-kernel-v81|",0)==0);
        CHECK(Simulation::from_snapshot(durable).canonical_state()==durable);
    }
    {
        PersistentWorld w(db.string(),Simulation(2));
        CHECK(w.world().canonical_state()==durable);
        CHECK(w.world().total_ore()==origin);
        CHECK(w.world().planetary_sites().size()==1);
        CHECK(w.world().planetary_shuttles().at(
            PlanetarySiteKey{chosen.body,chosen.latitude,chosen.longitude,1})
              .ascent_receipt==81003);
        CHECK(!w.launch_planetary_shuttle(7,ship,
            PlanetarySiteKey{chosen.body,chosen.latitude,chosen.longitude,1},
            81001,false));
        CHECK(!w.retrieve_planetary_site(8,ship,chosen.body,chosen.latitude,chosen.longitude,false,1));
        // No material or facility exists at an arbitrary neighboring tile.
        CHECK(!w.supply_planetary_site(7,ship,chosen.body,
            chosen.latitude,std::uint16_t((chosen.longitude+1)%72),true,1));
        CHECK(!w.support_planetary_crew(7,ship,chosen.body,
            chosen.latitude,chosen.longitude,true,-1));
        CHECK(w.world().planetary_spaceport_operational(
            chosen.body,chosen.latitude,chosen.longitude));
        CHECK(w.world().planetary_sites().at(
            PlanetarySiteKey{chosen.body,chosen.latitude,chosen.longitude,1})
              .delegated_npcs[0]==first_npc);
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
