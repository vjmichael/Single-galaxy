#include "galaxy/NpcAppearanceGenome.hpp"
#include "galaxy/Simulation.hpp"
#include "galaxy/Persistence.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <cmath>

namespace fs=std::filesystem;
using namespace galaxy;
#define REQUIRE(x) do{if(!(x))throw std::runtime_error(std::string("ART-02 line ")+std::to_string(__LINE__)+": " #x);}while(false)
struct Temporary {
    fs::path dir;
    Temporary():dir(fs::temp_directory_path()/("sg_art02_"+std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()))){fs::create_directories(dir);}
    ~Temporary(){std::error_code ec;fs::remove_all(dir,ec);}
};
template<typename F>void must_reject(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}REQUIRE(rejected);}
static constexpr std::uint64_t kChildBase=1ULL<<62;
float distance(const NpcAppearanceProfile& a,const NpcAppearanceProfile& b){
    auto m=a.facial_morphs(),n=b.facial_morphs();float result=0;
    for(std::size_t i=0;i<m.size();++i)result+=std::abs(m[i]-n[i]);
    return result/static_cast<float>(m.size());
}
void actual_simulation_boundary(const Temporary& temp){
    const auto db=(temp.dir/"actual_world.sqlite").string();
    std::uint64_t digest=0;
    {
        Simulation genesis(1200);genesis.add_colony_ship(7,{0,0},100,24);
        genesis.enable_individual_workforce();
        PersistentWorld world(db,std::move(genesis));
        REQUIRE(world.world().npc_records().size()==24);
        NpcAppearanceStore faces(db,"colony7",1200,[&](std::uint64_t id){
            return world.world().npc_records().count(id)!=0;
        });
        faces.register_settlement(7,24);
        faces.enroll_family_founder(1,7);faces.enroll_family_founder(2,7);
        auto approve=[](std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t){return true;};
        const auto child=faces.record_birth(1,kChildBase+1,1,2,7,1,0,approve);
        digest=child.appearance_digest;
        REQUIRE(child.parent_a==1&&child.parent_b==2);
        REQUIRE(child.census_revision==1&&faces.family_census(7).current_population==25);
        REQUIRE(faces.get(kChildBase+1).digest==digest);
        REQUIRE(faces.record_birth(1,kChildBase+1,2,1,7,1,0,approve).appearance_digest==digest);
        REQUIRE(faces.family_census(7).current_population==25);
        must_reject([&]{faces.record_birth(1,kChildBase+2,1,2,7,1,1,approve);});
        must_reject([&]{faces.record_birth(2,kChildBase+2,1,2,7,2,0,approve);});
        must_reject([&]{faces.record_birth(2,3,1,2,7,2,1,approve);});
        must_reject([&]{faces.record_birth(2,kChildBase+2,1,3,7,2,1,approve);});
        must_reject([&]{faces.record_birth(2,kChildBase+2,1,2,7,2,1,{});});
        must_reject([&]{faces.record_birth(2,kChildBase+2,1,2,7,2,1,
            [](auto,auto,auto,auto){return false;});});
        must_reject([&]{faces.register_settlement(7,23);});
        // Vital boundary: this subsystem has not yet spawned NPCs or changed
        // the core crew ledger. This is a cohort-only birth pending promotion.
        REQUIRE(world.world().npc_records().count(kChildBase+1)==0);
        REQUIRE(world.world().npc_records().size()==24);
        world.step();
    }
    {
        PersistentWorld world(db,Simulation(999));
        REQUIRE(world.world().npc_records().size()==24);
        NpcAppearanceStore faces(db,"colony7",1200,[&](std::uint64_t id){
            return world.world().npc_records().count(id)!=0;
        });
        REQUIRE(faces.family_census(7).current_population==25);
        REQUIRE(faces.get_birth(1).appearance_digest==digest);
        REQUIRE(faces.get(kChildBase+1).digest==digest);
        must_reject([&]{faces.ensure_founder(kChildBase+1);});
        must_reject([&]{faces.enroll_family_founder(kChildBase+1,7);});
    }
    std::cout<<"ART02 real PersistentWorld founder admission and restart boundary PASS\n";
}
void failure_recovery(const Temporary& temp){
    const auto db=(temp.dir/"faults.sqlite").string();
    NpcAppearanceStore faces(db,"faults",1444,[](std::uint64_t id){return id==10||id==11;});
    faces.register_settlement(3,25);
    faces.enroll_family_founder(10,3);faces.enroll_family_founder(11,3);
    // Sabotage journal insertion: a partial child profile must not survive.
    sqlite3* raw=nullptr;REQUIRE(sqlite3_open(db.c_str(),&raw)==SQLITE_OK);
    char* err=nullptr;
    REQUIRE(sqlite3_exec(raw,"CREATE TRIGGER reject_birth BEFORE INSERT ON npc_family_births "
        "BEGIN SELECT RAISE(ABORT,'simulated SQLite failure'); END",nullptr,nullptr,&err)==SQLITE_OK);
    auto approve=[](auto,auto,auto,auto){return true;};
    must_reject([&]{faces.record_birth(50,kChildBase+50,10,11,3,1,0,approve);});
    REQUIRE(faces.family_census(3).current_population==25);
    REQUIRE(faces.family_census(3).revision==0);
    REQUIRE(!faces.exists(kChildBase+50));
    must_reject([&]{faces.get_birth(50);});
    REQUIRE(sqlite3_exec(raw,"DROP TRIGGER reject_birth",nullptr,nullptr,&err)==SQLITE_OK);
    sqlite3_close(raw);
    auto birth=faces.record_birth(50,kChildBase+50,10,11,3,1,0,approve);
    REQUIRE(birth.appearance_digest==faces.get(kChildBase+50).digest);
    REQUIRE(faces.family_census(3).current_population==26);
    // Births are append-only even for direct SQL writers.
    REQUIRE(sqlite3_open(db.c_str(),&raw)==SQLITE_OK);
    REQUIRE(sqlite3_exec(raw,"DELETE FROM npc_family_births",nullptr,nullptr,&err)!=SQLITE_OK);
    if(err){sqlite3_free(err);err=nullptr;}
    sqlite3_close(raw);
    std::cout<<"ART02 SQLite injected failure, complete rollback and retry PASS\n";
}
void hundred_families(const Temporary& temp){
    const auto db=(temp.dir/"generations.sqlite").string();
    const std::uint64_t seed=9202,settlement=19;
    auto admitted=[](std::uint64_t id){return id>=1000&&id<2000;};
    std::vector<std::uint64_t> digests;
    double parental=0,unrelated=0;
    {
        NpcAppearanceStore s(db,"100-families",seed,admitted);
        s.register_settlement(settlement,1000000);
        // 3 real founder identities per family; two children and a grandchild.
        for(std::uint64_t i=0;i<100;++i){
            const auto a=1000+i*3,b=a+1,c=a+2;
            s.enroll_family_founder(a,settlement);
            s.enroll_family_founder(b,settlement);
            s.enroll_family_founder(c,settlement);
        }
        auto approve=[](auto,auto,auto,auto){return true;};
        for(std::uint64_t i=0;i<100;++i){
            const auto a=1000+i*3,b=a+1,c=a+2;
            const auto child=kChildBase+i*3, sibling=child+1,grandchild=child+2;
            const auto r1=s.record_birth(i*3+1,child,a,b,settlement,100+i,i*3,approve);
            const auto r2=s.record_birth(i*3+2,sibling,a,b,settlement,100+i,i*3+1,approve);
            const auto r3=s.record_birth(i*3+3,grandchild,child,c,settlement,101+i,i*3+2,approve);
            REQUIRE(r1.appearance_digest!=r2.appearance_digest);
            REQUIRE(s.get(grandchild).generation==2);
            REQUIRE(r3.census_revision==i*3+3);
            parental+=0.5*(distance(s.get(child),s.get(a))+distance(s.get(child),s.get(b)));
            unrelated+=distance(s.get(child),s.get(1000+((i+51)%100)*3));
            digests.push_back(s.get(child).digest);
        }
        REQUIRE(s.family_census(settlement).current_population==1000300);
        REQUIRE(s.family_census(settlement).revision==300);
        REQUIRE(s.named_profile_count()==600); // 300 founders and 300 births
        REQUIRE(parental/100.0 < unrelated/100.0 * 0.84);
        must_reject([&]{s.record_death(9001,kChildBase,settlement,9000,300,
            [](auto,auto,auto){return false;});});
        REQUIRE(s.family_census(settlement).current_population==1000300);
        const auto loss=s.record_death(9001,kChildBase,settlement,9000,300,
            [](auto,auto,auto){return true;});
        REQUIRE(loss.census_revision==301);
        REQUIRE(s.family_census(settlement).current_population==1000299);
        REQUIRE(s.record_death(9001,kChildBase,settlement,9000,300,
            [](auto,auto,auto){return true;}).census_revision==301);
        must_reject([&]{s.record_death(9002,kChildBase,settlement,9001,301,
            [](auto,auto,auto){return true;});});
        must_reject([&]{s.record_birth(9003,kChildBase+400,kChildBase,1002,
            settlement,9002,301,approve);});
        REQUIRE(s.family_census(settlement).current_population==1000299);
    }
    {
        NpcAppearanceStore s(db,"100-families",seed,admitted);
        REQUIRE(s.family_census(settlement).current_population==1000299);
        REQUIRE(s.get_death(9001).npc_id==kChildBase);
        for(std::uint64_t i=0;i<100;++i){
            const auto child=kChildBase+i*3;
            REQUIRE(s.get(child).digest==digests[i]);
            REQUIRE(s.get_birth(i*3+1).child_id==child);
        }
        auto approve=[](auto,auto,auto,auto){return true;};
        REQUIRE(s.record_birth(1,kChildBase,1000,1001,settlement,100,0,approve).appearance_digest==digests.front());
        REQUIRE(s.family_census(settlement).current_population==1000299);
    }
    std::cout<<"ART02 100 families, 300 births and 1 death over 3 generations, child-to-parent "
             <<parental/100.0<<" versus unrelated "<<unrelated/100.0<<" PASS\n";
}
int main(){
    try{
        Temporary temp;
        actual_simulation_boundary(temp);
        failure_recovery(temp);
        hundred_families(temp);
        std::cout<<"ART-02 population/census/genome transactional lineage tests PASS\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"ART-02 FAILED: "<<e.what()<<'\n';return 1;
    }
}
