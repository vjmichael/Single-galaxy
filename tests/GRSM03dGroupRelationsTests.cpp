#include "galaxy/Persistence.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(expr) do { if (!(expr)) throw std::runtime_error("GRSM03d line " + std::to_string(__LINE__) + ": " #expr); } while (false)
struct Temp {
    fs::path folder=fs::temp_directory_path()/("grsm03d_"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temp(){fs::create_directories(folder);}
    ~Temp(){std::error_code ec;fs::remove_all(folder,ec);}
};
static void sql(const fs::path& path,const std::string& statement){
    sqlite3* db=nullptr;CHECK(sqlite3_open(path.string().c_str(),&db)==SQLITE_OK);
    char* error=nullptr;const int code=sqlite3_exec(db,statement.c_str(),nullptr,nullptr,&error);
    const std::string why=error?error:"";sqlite3_free(error);sqlite3_close(db);
    if(code!=SQLITE_OK)throw std::runtime_error("fixture SQL: "+why);
}
static bool rejects(const fs::path& path){
    try{PersistentWorld world(path.string(),Simulation(999));return false;}
    catch(const std::exception&){return true;}
}
static std::uint64_t meeting(const PersistentWorld& world,std::uint64_t a,std::uint64_t b){
    for(const auto& event:world.recent_social_encounters(128))
        if(event.npc_low==std::min(a,b)&&event.npc_high==std::max(a,b))return event.id;
    return 0;
}
static void diverse_views(const fs::path& root){
    const auto db=root/"groups.sqlite",copy=root/"recover.sqlite";
    Simulation sim(71);
    const auto ship=sim.add_colony_ship(3,{0,0},100,6);
    sim.add_colony_ship(5,{25000,0},100,3);
    sim.enable_individual_workforce();
    std::uint64_t guild=0,guard=0,volunteers=0;
    {
        PersistentWorld world(db.string(),std::move(sim));
        guild=world.found_social_group("Mining Guild",{1,2,3});
        guard=world.found_social_group("Defense Union",{4,5,6});
        volunteers=world.found_social_group("Joint Rescue Volunteers",{2,4});
        CHECK(guild==1&&guard==2&&volunteers==3);
        CHECK(world.group_relation_summary(guard,guild).informed_members==0);
        CHECK(world.individual_group_feeling(4,guard,guild)==0);
        // All contacts are bounded, physically co-located and explicitly scheduled.
        for(int i=0;i<5;++i)world.schedule_social_encounters(64,8);
        CHECK(meeting(world,1,4)&&meeting(world,2,5)&&meeting(world,3,6)&&meeting(world,2,4));
        CHECK(!world.attribute_interaction_to_group(0,guard,guild,GroupAttributionStance::Appreciative));
        CHECK(!world.attribute_interaction_to_group(99,guard,guild,GroupAttributionStance::Appreciative));
        CHECK(!world.record_social_interaction(meeting(world,1,4),1,7,SocialInteractionKind::Assistance));
        CHECK(world.record_social_interaction(meeting(world,1,4),4,1,SocialInteractionKind::Assistance));
        const auto rescue=world.recent_social_interactions().front().id;
        CHECK(!world.attribute_interaction_to_group(rescue,guild,guard,GroupAttributionStance::Appreciative));
        CHECK(!world.attribute_interaction_to_group(rescue,guard,guard,GroupAttributionStance::Appreciative));
        CHECK(!world.attribute_interaction_to_group(rescue,guard,99,GroupAttributionStance::Appreciative));
        CHECK(world.attribute_interaction_to_group(rescue,guard,guild,GroupAttributionStance::Appreciative));
        CHECK(!world.attribute_interaction_to_group(rescue,guard,guild,GroupAttributionStance::Resentful));
        CHECK(world.record_social_interaction(meeting(world,2,5),5,2,SocialInteractionKind::Harm));
        const auto blame=world.recent_social_interactions().front().id;
        CHECK(world.attribute_interaction_to_group(blame,guard,guild,GroupAttributionStance::Resentful));
        CHECK(world.record_social_interaction(meeting(world,3,6),6,3,SocialInteractionKind::Assistance));
        const auto mixed=world.recent_social_interactions().front().id;
        CHECK(world.attribute_interaction_to_group(mixed,guard,guild,GroupAttributionStance::Ambivalent));
        auto summary=world.group_relation_summary(guard,guild);
        CHECK(summary.current_members==3&&summary.informed_members==3);
        CHECK(summary.favorable==1&&summary.unfavorable==1&&summary.neutral==1);
        CHECK(summary.mean_feeling==-1); // (8-12+0)/3; NOT policy or unanimous feeling.
        CHECK(world.individual_group_feeling(4,guard,guild)==8);
        CHECK(world.individual_group_feeling(5,guard,guild)==-12);
        CHECK(world.individual_group_feeling(6,guard,guild)==0);
        CHECK(world.individual_group_feeling(1,guild,guard)==0); // no mirrored stance
        CHECK(world.record_social_interaction(meeting(world,1,4),1,4,SocialInteractionKind::Harm));
        const auto reverse=world.recent_social_interactions().front().id;
        CHECK(world.attribute_interaction_to_group(reverse,guild,guard,GroupAttributionStance::Resentful));
        CHECK(world.group_relation_summary(guild,guard).informed_members==1);
        CHECK(world.group_relation_summary(guild,guard).unfavorable==1);
        // The same personally witnessed source can be interpreted through
        // distinct overlapping group affiliations without silently voting.
        CHECK(world.record_social_interaction(meeting(world,2,4),4,2,SocialInteractionKind::Assistance));
        const auto overlap=world.recent_social_interactions().front().id;
        CHECK(world.attribute_interaction_to_group(overlap,volunteers,guild,GroupAttributionStance::Appreciative));
        CHECK(world.attribute_interaction_to_group(overlap,guard,guild,GroupAttributionStance::Resentful));
        CHECK(world.individual_group_feeling(4,volunteers,guild)==8);
        CHECK(world.individual_group_feeling(4,guard,guild)==-4); // context-dependent opinions
        CHECK(world.group_relation_summary(volunteers,guild).informed_members==1);
        CHECK(world.group_relation_summary(guard,guild).favorable==0);
        // Membership exit doesn't rewrite historical testimony, but a current
        // group summary excludes departed members until they rejoin.
        CHECK(world.change_group_membership(guard,5,GroupMembershipKind::Leave));
        CHECK(world.group_relation_summary(guard,guild).current_members==2);
        CHECK(world.group_relation_summary(guard,guild).informed_members==2);
        CHECK(world.individual_group_feeling(5,guard,guild)==-12); // retained personal evidence
        CHECK(world.change_group_membership(guard,5,GroupMembershipKind::Join,4));
        CHECK(world.group_relation_summary(guard,guild).informed_members==3);
        CHECK(world.recent_knowledge().empty());
        CHECK(world.recent_group_views().empty());
        CHECK(world.recent_social_ties().empty());
        CHECK(world.world().colony_ship(ship)->crew==6);
        world.steps(6);
        CHECK(!world.attribute_interaction_to_group(reverse,guild,guard,GroupAttributionStance::Appreciative));
        PersistentWorld::recover_copy(db.string(),copy.string());
    }
    for(const auto& saved:{db,copy}){
        PersistentWorld world(saved.string(),Simulation(88));
        CHECK(world.recent_group_attributions().size()==6);
        CHECK(world.group_relation_summary(guard,guild).informed_members==3);
        CHECK(world.group_relation_summary(guild,guard).informed_members==1);
        CHECK(world.individual_group_feeling(4,volunteers,guild)==8);
        CHECK(world.individual_group_feeling(4,guard,guild)==-4);
        CHECK(world.recent_knowledge().empty());
    }
    for(const auto& [label,attack]:std::vector<std::pair<std::string,std::string>>{
        {"fake_actor","DROP TRIGGER group_attributions_no_update; UPDATE group_attributions SET actor='99' WHERE id=1;"},
        {"false_member","DROP TRIGGER group_attributions_no_update; UPDATE group_attributions SET observer='1' WHERE id=1;"},
        {"changed_source","DROP TRIGGER group_attributions_no_update; UPDATE group_attributions SET interaction_id=99 WHERE id=1;"},
        {"changed_stance","DROP TRIGGER group_attributions_no_update; UPDATE group_attributions SET stance=2 WHERE id=1;"},
        {"changed_score","DROP TRIGGER group_attributions_no_update; UPDATE group_attributions SET feeling=99 WHERE id=1;"},
        {"changed_epoch","DROP TRIGGER group_attributions_no_update; UPDATE group_attributions SET membership_seq=99 WHERE id=1;"},
        {"changed_metadata","UPDATE attribution_meta SET event_count=999 WHERE singleton=1;"},
        {"deleted","DROP TRIGGER group_attributions_no_delete; DELETE FROM group_attributions WHERE id=3;"}
    }){
        const auto bad=root/(label+".sqlite");fs::copy_file(db,bad);
        sql(bad,attack);CHECK(rejects(bad));
    }
}
static void actual_v12_upgrade(const fs::path& root){
    const auto db=root/"old_v12.sqlite";
    Simulation sim(8);sim.add_colony_ship(2,{0,0},100,4);sim.enable_individual_workforce();
    {
        PersistentWorld world(db.string(),std::move(sim));
        CHECK(world.found_social_group("A",{1,2})==1);
        CHECK(world.found_social_group("B",{3,4})==2);
        CHECK(world.schedule_social_encounters(64,8)>0);
        CHECK(world.record_social_interaction(meeting(world,1,3),3,1,SocialInteractionKind::Assistance));
        CHECK(world.recent_group_attributions().empty());
    }
    // Genuine v12 shape retained physical and interaction records but had
    // no group-attribution table or metadata.
    sql(db,"DROP TABLE group_attributions; DROP TABLE attribution_meta; PRAGMA user_version=12;");
    {
        PersistentWorld world(db.string(),Simulation(7));
        CHECK(world.recent_social_interactions().size()==1);
        CHECK(world.recent_group_attributions().empty());
        CHECK(world.group_relation_summary(2,1).informed_members==0);
        CHECK(world.attribute_interaction_to_group(1,2,1,GroupAttributionStance::Appreciative));
        CHECK(world.group_relation_summary(2,1).favorable==1);
    }
    PersistentWorld reloaded(db.string(),Simulation(9));
    CHECK(reloaded.group_relation_summary(2,1).mean_feeling==8);
}
int main(){Temp t;try{
    diverse_views(t.folder);
    std::cout<<"PASS individual disagreements, cross-group asymmetry, overlapping memberships, recovery, tamper rejection\n";
    actual_v12_upgrade(t.folder);
    std::cout<<"PASS real SQLite v12 to v13 migration without fabricated group opinions\n";
    std::cout<<"GRSM-03d 2/2 acceptance scenarios passed\n";
    return 0;
}catch(const std::exception& e){
    std::cerr<<"GRSM-03d FAIL: "<<e.what()<<'\n';return 1;
}}
