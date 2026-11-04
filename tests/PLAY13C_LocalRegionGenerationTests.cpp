#include "galaxy/Persistence.hpp"
#include "galaxy/ClientContract.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string("PLAY13C4: ")+#x+" @"+std::to_string(__LINE__));}while(false)
static bool rejects(const std::string& s){
    try{(void)Simulation::from_snapshot(s);return false;}
    catch(const std::exception&){return true;}
}
int main(){
    auto dir=fs::temp_directory_path()/
      ("sg_play13c4_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try{
        fs::create_directories(dir);
        const auto db=(dir/"world.sqlite").string(),copy=(dir/"copy.sqlite").string();
        EntityId ship=0,donor=0,first_rock=0;
        std::uint64_t first_body=0,other_body=0;
        std::int64_t initial_mass=0;
        std::string after_mine;
        {
            Simulation seed(13004);
            ship=seed.add_colony_ship(7,{0,0},60,8);
            donor=seed.add_asteroid({30000,0},60);
            seed.provision_starter_exploration_scanner(ship);
            initial_mass=seed.total_material_kg();
            PersistentWorld w(db,std::move(seed));
            w.ensure_physical_celestial_systems();
            w.ensure_ship_frame_registry();
            const auto& bodies=w.world().physical_celestial_systems().at(1);
            CHECK(bodies.size()>1);
            first_body=bodies.at(0).id;other_body=bodies.at(1).id;
            CHECK(w.begin_celestial_transit(7,ship,first_body,501));
            w.steps(4);
            const auto before_arrival=w.world().canonical_state();
            bool interrupted=false;
            try{(void)w.complete_celestial_transit(501,FailPoint::AfterSnapshotWrite);}
            catch(const std::exception&){interrupted=true;}
            CHECK(interrupted);
            CHECK(w.world().canonical_state()==before_arrival);
            CHECK(w.world().generated_local_regions().empty());
            CHECK(w.world().total_material_kg()==initial_mass);
            CHECK(w.complete_celestial_transit(501));
            CHECK(w.world().canonical_state().rfind("galaxy-kernel-v74|",0)==0);
            CHECK(w.world().generated_local_regions().count(first_body)==1);
            CHECK(w.world().local_entity_anchors().size()==2);
            CHECK(w.world().entities().at(donor).ore==46);
            CHECK(w.world().total_material_kg()==initial_mass);
            CHECK(w.world().coarse_probe_signatures(7,ship).size()==2);
            first_rock=w.world().local_entity_anchors().begin()->first;
            CHECK(w.world().same_local_frame(ship,first_rock));
            CHECK(!w.world().same_local_frame(ship,donor));
            ServerViewBridge bridge(w.world(),9412,7);
            ClientView client;
            CHECK(client.apply(decode_frame(encode_frame(bridge.snapshot())))==ApplyResult::Applied);
            CHECK(client.world().entities.count(first_rock)==1);
            CHECK(client.world().entities.count(donor)==0);
            Command scan{};
            scan.id=600;scan.sequence=600;scan.execute_tick=w.world().tick()+1;
            scan.issuer=7;scan.actor=ship;scan.action=Action::SurveyLocal;
            CHECK(w.submit(scan));w.step();
            CHECK(w.world().events().back().result==Outcome::Applied);
            CHECK(w.world().events().back().transferred==2);
            Command mine{};
            mine.id=601;mine.sequence=601;mine.execute_tick=w.world().tick()+1;
            mine.issuer=7;mine.actor=ship;mine.target=first_rock;
            mine.action=Action::Mine;mine.amount=6;
            CHECK(w.submit(mine));w.step();
            CHECK(w.world().events().back().result==Outcome::Applied);
            CHECK(w.world().events().back().transferred==6);
            CHECK(w.world().entities().count(first_rock)==0);
            CHECK(w.world().local_entity_anchors().count(first_rock)==0);
            CHECK(w.world().generated_local_regions().count(first_body)==1);
            CHECK(w.world().total_material_kg()==initial_mass);
            after_mine=w.world().canonical_state();
            CHECK(Simulation::from_snapshot(after_mine).canonical_state()==after_mine);
            auto tampered=after_mine;
            const auto idx=tampered.find("localanchor|");
            CHECK(idx!=std::string::npos);
            const auto end=tampered.find('\n',idx);
            tampered.erase(idx,end-idx+1);
            CHECK(rejects(tampered));
            PersistentWorld::recover_copy(db,copy);
        }
        {
            PersistentWorld w(db,Simulation(8));
            CHECK(w.world().canonical_state()==after_mine);
            CHECK(w.world().local_entity_anchors().size()==1);
            CHECK(w.world().total_material_kg()==initial_mass);
            CHECK(w.begin_celestial_transit(7,ship,other_body,502));
            w.steps(4);CHECK(w.complete_celestial_transit(502));
            CHECK(w.world().generated_local_regions().size()==2);
            CHECK(w.world().total_material_kg()==initial_mass);
            const auto remaining_before=w.world().entities().size();
            CHECK(w.begin_celestial_transit(7,ship,first_body,503));
            w.steps(4);CHECK(w.complete_celestial_transit(503));
            CHECK(w.world().entities().size()==remaining_before);
            CHECK(w.world().local_entity_anchors().count(first_rock)==0);
            CHECK(w.world().generated_local_regions().size()==2);
            CHECK(w.world().total_material_kg()==initial_mass);
        }
        {
            PersistentWorld w(copy,Simulation(9));
            CHECK(w.world().canonical_state()==after_mine);
            CHECK(w.world().colony_ship(ship)->crew==8);
            CHECK(w.world().total_material_kg()==initial_mass);
        }
        // An empty genesis world never materializes ore from a star catalogue.
        {
            Simulation empty(42);
            const auto alone=empty.add_colony_ship(7,{0,0},60,8);
            empty.provision_starter_exploration_scanner(alone);
            PersistentWorld w((dir/"barren.sqlite").string(),std::move(empty));
            w.ensure_physical_celestial_systems();
            w.ensure_ship_frame_registry();
            const auto body=w.world().physical_celestial_systems().at(1).front().id;
            CHECK(w.begin_celestial_transit(7,alone,body,700));
            w.steps(4);CHECK(w.complete_celestial_transit(700));
            CHECK(w.world().generated_local_regions().count(body)==1);
            CHECK(w.world().local_entity_anchors().empty());
            CHECK(w.world().total_ore()==0);
        }
        fs::remove_all(dir);
        std::cout<<"PASS PLAY13C4 bounded physical region generation, mining, no mint, reload\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL PLAY13C4 "<<e.what()<<'\n';
        std::error_code ec;fs::remove_all(dir,ec);return 1;
    }
}
