#include "galaxy/NpcAppearanceGenome.hpp"
#include "galaxy/Simulation.hpp"
#include "galaxy/Persistence.hpp"

#include <sqlite3.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs=std::filesystem;
using namespace galaxy;
#define CHECK(condition) do{if(!(condition))throw std::runtime_error(\
    std::string("appearance test line ")+std::to_string(__LINE__)+": " #condition);}while(0)

struct Temp {
    fs::path path;
    Temp():path(fs::temp_directory_path()/("sg_npc_face_"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {fs::create_directories(path);}
    ~Temp(){std::error_code ec;fs::remove_all(path,ec);}
};
template<typename Function> void rejects(Function&& f){
    bool failed=false;try{f();}catch(const std::exception&){failed=true;}CHECK(failed);
}
float face_distance(const NpcAppearanceProfile& a,const NpcAppearanceProfile& b){
    const auto x=a.facial_morphs(),y=b.facial_morphs();float sum=0;
    for(std::size_t i=0;i<x.size();++i){sum+=std::abs(x[i]-y[i]);}
    return sum/static_cast<float>(x.size());
}
void population_and_world_integration(const Temp& tmp){
    const auto db=(tmp.path/"persistent_world.sqlite").string();
    std::uint64_t digest=0;
    {
        Simulation sim(1234);
        sim.add_colony_ship(7,{0,0},100,24);
        sim.enable_individual_workforce();
        CHECK(sim.npc_records().size()==24);
        PersistentWorld world(db,std::move(sim));
        const auto admit=[&world](std::uint64_t id){return world.world().npc_records().count(id)!=0;};
        NpcAppearanceStore faces(db,"galaxy:alpha",1234,admit);
        for(std::uint64_t id=1;id<=24;++id){
            const auto p=faces.ensure_founder(id);
            CHECK(p.npc_id==id&&p.generation==0);
            CHECK(faces.ensure_founder(id).digest==p.digest);
        }
        digest=faces.get(7).digest;
        CHECK(faces.named_profile_count()==24);
        rejects([&]{faces.ensure_founder(25);}); // no phantom NPC
        rejects([&]{faces.ensure_child(1,2,3);}); // identity cannot be overwritten
        rejects([&]{faces.ensure_child(999,1,2);});
        for(const auto& [id,n]:world.world().npc_records()) {
            (void)n;const auto traits=faces.get(id).facial_morphs();
            for(auto x:traits)CHECK(std::isfinite(x)&&x>=0.0f&&x<=1.0f);
        }
        CHECK(world.world().npc_records().size()==24);
        world.step(); // actual PersistentWorld activity while facial records exist
        CHECK(faces.get(7).digest==digest);
    }
    {
        PersistentWorld world(db,Simulation(8));
        CHECK(world.world().npc_records().size()==24);
        NpcAppearanceStore faces(db,"galaxy:alpha",1234,[&world](std::uint64_t id){
            return world.world().npc_records().count(id)!=0;
        });
        CHECK(faces.named_profile_count()==24);
        CHECK(faces.get(7).digest==digest);
        rejects([&]{faces.ensure_child(3,2,4);});
        rejects([&]{NpcAppearanceStore wrong(db,"galaxy:alpha",456,[](std::uint64_t){return true;});});
    }
}
void synthetic_family_recombination(const Temp& tmp){
    const auto db=(tmp.path/"families.sqlite").string();
    std::set<std::uint64_t> admitted;
    auto authority=[&](std::uint64_t id){return admitted.count(id)!=0;};
    double familial=0,unrelated=0;
    std::vector<std::uint64_t> checksums;
    {
        NpcAppearanceStore store(db,"colony-families",996633,authority);
        for(std::uint64_t i=0;i<100;++i){
            const std::uint64_t start=1000+i*10;
            for(std::uint64_t k=0;k<7;++k)admitted.insert(start+k);
            const auto a=store.ensure_founder(start);
            const auto b=store.ensure_founder(start+1);
            const auto unrelated_person=store.ensure_founder(start+6);
            const auto child=store.ensure_child(start+2,start+1,start); // reversed parent order
            const auto sibling=store.ensure_child(start+3,start,start+1);
            const auto partner=store.ensure_founder(start+4);
            const auto grandchild=store.ensure_child(start+5,start+2,start+4);
            CHECK(child.parent_a==start&&child.parent_b==start+1&&child.generation==1);
            CHECK(grandchild.generation==2);
            CHECK(child.digest!=sibling.digest);
            CHECK(store.ensure_child(start+2,start,start+1).digest==child.digest);
            rejects([&]{store.ensure_child(start+2,start+1,start+4);});
            rejects([&]{store.ensure_child(start+3,start,start);});
            rejects([&]{store.ensure_child(start+3,start+3,start+1);});
            familial+=(face_distance(child,a)+face_distance(child,b))*0.5;
            unrelated+=face_distance(child,unrelated_person);
            checksums.push_back(child.digest);
        }
        CHECK(store.named_profile_count()==700);
        CHECK(familial/100.0<unrelated/100.0*0.78); // population-level resemblance
    }
    {
        NpcAppearanceStore store(db,"colony-families",996633,authority);
        CHECK(store.named_profile_count()==700);
        for(std::uint64_t i=0;i<100;++i){
            const std::uint64_t start=1000+i*10;
            CHECK(store.get(start+2).digest==checksums[i]);
            CHECK(store.ensure_child(start+2,start,start+1).digest==checksums[i]);
            CHECK(store.get(start+5).generation==2);
        }
    }
    // Two separately seeded worlds must not silently mix records in one scope.
    rejects([&]{NpcAppearanceStore wrong(db,"colony-families",123,authority);});
    // A valid other scope is still isolated from the first world.
    {NpcAppearanceStore other(db,"different-galaxy",444,authority);
     CHECK(other.named_profile_count()==0);
     CHECK(other.ensure_founder(1000).digest!=checksums.front());}
    // Corrupt one byte of stored genotype: registry must reject the changed face.
    sqlite3* raw=nullptr;
    CHECK(sqlite3_open(db.c_str(),&raw)==SQLITE_OK);
    char* err=nullptr;
    const int status=sqlite3_exec(raw,
        "UPDATE npc_appearance_profiles SET alleles=zeroblob(160) "
        "WHERE scope='colony-families' AND npc_id='1002'",nullptr,nullptr,&err);
    if(err)sqlite3_free(err);
    sqlite3_close(raw);
    CHECK(status==SQLITE_OK);
    {NpcAppearanceStore store(db,"colony-families",996633,authority);
     rejects([&]{store.get(1002);});
     rejects([&]{store.ensure_child(1002,1000,1001);});
     CHECK(store.get(1012).digest==checksums[1]);}
    std::cout<<"100 families / 700 persistent named profiles / three generations; "
             <<"mean family distance="<<familial/100.0<<", unrelated="<<unrelated/100.0<<"\n";
}
int main(){
    try{
        Temp tmp;
        population_and_world_integration(tmp);
        synthetic_family_recombination(tmp);
        std::cout<<"NPC appearance genome: deterministic inheritance, real NPC admission, "
                    "SQLite restart and corruption checks PASS\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"NPC appearance genome FAILED: "<<e.what()<<'\n';
        return 1;
    }
}
