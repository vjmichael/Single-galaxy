#include "galaxy/SecurityDeskHost.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(#x)+" at "+std::to_string(__LINE__));}while(false)

static std::pair<EntityId,EntityId> seed(const std::string& db){
    Simulation initial(7077);
    auto a=initial.add_colony_ship(7,{0,0},100,6);
    auto b=initial.add_colony_ship(8,{900,0},100,6);
    initial.provision_colony_genesis(a,2000,2000,2000,2000,2000);
    initial.provision_colony_genesis(b,2000,2000,2000,2000,2000);
    initial.enable_individual_workforce();
    initial.upgrade_assignment_snapshot_format();
    initial.enable_passenger_life_support();
    PersistentWorld origin(db,std::move(initial));
    CHECK(origin.world().find(a)->owner_id==7);
    CHECK(origin.world().find(b)->owner_id==8);
    CHECK(origin.world().colony_ship(a)!=nullptr);
    CHECK(origin.world().colony_ship(b)!=nullptr);
    return {a,b};
}
int main(int argc,char** argv){
 if(argc==3 && std::string(argv[1])=="--seed"){
    try{
        CHECK(!fs::exists(argv[2]));
        const auto pair=seed(argv[2]);
        // The fixture is created deliberately for integration acceptance.
        // Normal single-player genesis is never given a free second colony.
        std::cout<<"PASS fixture original players 7/8 ships "<<pair.first<<" "<<pair.second<<"\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL seeded native two-human fixture "<<e.what()<<'\n';
        return 1;
    }
 }
 if(argc!=1)return 2;
 const auto dir=fs::temp_directory_path()/
    ("sg_status07g_"+std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
 try{
    fs::create_directories(dir);
    const auto db=(dir/"world.sqlite").string();
    const auto after=(dir/"restarted.sqlite").string();
    auto [a,b]=seed(db);
    std::string canonical;
    {
        PersistentWorld w(db,Simulation(7));
        colony_disposition::TerritoryJournal territory(db);
        colony_disposition::CyberIncidentJournal cases(db);
        ship_comms::Journal comms(db);
        auto read=[&](PlayerId who,EntityId home){
            std::vector<std::string> frames;
            colony_disposition::security_desk::snapshot(
                w,territory,cases,who,home,home,
                [&](const std::string& msg){frames.push_back(msg);},
                &comms);
            return frames;
        };
        auto a0=read(7,a),b0=read(8,b);
        CHECK(std::none_of(a0.begin(),a0.end(),
            [](const auto& m){return m.rfind("SECURITY_CALL_INBOX|",0)==0;}));
        CHECK(std::none_of(b0.begin(),b0.end(),
            [](const auto& m){return m.rfind("SECURITY_CALL_INBOX|",0)==0;}));
        std::uint64_t nonce=910000000000ULL;
        std::vector<std::string> out;
        CHECK(colony_disposition::security_desk::execute(
            {"SECURITY_CALL",std::to_string(b)},w,territory,cases,
            7,a,a,nonce,[&](const std::string& m){out.push_back(m);},
            &comms));
        CHECK(comms.history().size()==1);
        const auto request=comms.history().front().event_id;
        CHECK(comms.history().front().caller_owner==7);
        CHECK(comms.history().front().recipient_ship==b);
        CHECK(comms.pending_for(w,7,a).empty());
        CHECK(comms.pending_for(w,8,b).size()==1);
        const auto av=read(7,a);
        CHECK(std::none_of(av.begin(),av.end(),
            [](const auto& m){return m.rfind("SECURITY_CALL_INBOX|",0)==0;}));
        // Call receipt is visible to its initiating owner but the incoming
        // inbox and authenticated controller stay restricted to recipient.

        const auto bv=read(8,b);
        CHECK(std::any_of(bv.begin(),bv.end(),
            [&](const std::string& m){
                return m=="SECURITY_CALL_INBOX|"+std::to_string(request)+"|"+
                     std::to_string(a)+"|7";
            }));
        // User 7 cannot submit a reply on someone else's actual hull.
        CHECK(comms.reply(w,7,a,request,++nonce,true).status!=
              ship_comms::Status::Identified);
        CHECK(comms.pending_for(w,8,b).size()==1);
        out.clear();
        CHECK(colony_disposition::security_desk::execute(
            {"SECURITY_CALL_REPLY",std::to_string(request),"IDENTIFY"},
            w,territory,cases,8,b,b,nonce,
            [&](const std::string& m){out.push_back(m);},&comms));
        CHECK(comms.pending_for(w,8,b).empty());
        CHECK(comms.history().size()==2);
        CHECK(comms.history().back().recipient_controller==8);
        CHECK(comms.history().back().kind==ship_comms::Kind::Identify);
        CHECK(!comms.penalty_for(w,8,b).penalty_points);
        // The verified caller's next unsolicited hail is suppressed and
        // penalized, but never converted into a criminal conviction.
        CHECK(colony_disposition::security_desk::execute(
            {"SECURITY_CALL",std::to_string(b)},w,territory,cases,
            7,a,a,nonce,[&](const std::string& m){out.push_back(m);},
            &comms));
        CHECK(comms.history().size()==3);
        CHECK(comms.history().back().kind==ship_comms::Kind::Abuse);
        CHECK(comms.penalty_for(w,7,a).penalty_points==1);
        CHECK(comms.pending_for(w,8,b).empty());
        CHECK(w.world().npc_flight_orders().empty());
        canonical=w.world().canonical_state();
        comms.verify();
        PersistentWorld::recover_copy(db,after);
    }
    {
        PersistentWorld w(after,Simulation(2));
        ship_comms::Journal comms(after);
        CHECK(w.world().canonical_state()==canonical);
        CHECK(comms.history().size()==3);
        CHECK(comms.penalty_for(w,7,a).penalty_points==1);
        CHECK(comms.pending_for(w,8,b).empty());
        comms.verify();
    }
    fs::remove_all(dir);
    std::cout<<"PASS STATUS-07G: original human owners 7/8 private hailing, owner-crossing denial, identified offender penalty, immutable SQLite restart\n";
    return 0;
 }catch(const std::exception& e){
    std::cerr<<"FAIL STATUS-07G "<<e.what()<<'\n';
    std::error_code ignored;fs::remove_all(dir,ignored);return 1;
 }
}
