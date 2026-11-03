#include "galaxy/Persistence.hpp"
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <sqlite3.h>
using namespace galaxy;
static void ck(bool b, const char* why){if(!b)throw std::runtime_error(why);}
static Command action(std::uint64_t command,std::uint64_t tick,Action kind,EntityId ferry,
                      EntityId to,std::uint64_t voyage,std::vector<std::uint64_t> ids){
    Command c{};c.id=command;c.execute_tick=tick;c.issuer=Simulation::kServerAuthority;
    c.actor=ferry;c.target=to;c.action=kind;c.transport_id=voyage;c.passenger_ids=std::move(ids);return c;
}
static std::string alter(const std::string& text,const std::string& prefix,int col,const std::string& value){
    auto found=text.find(prefix);ck(found!=std::string::npos,"record missing");
    auto end=text.find('\n',found);
    std::string line=text.substr(found,end-found);
    std::vector<std::string> fields;std::stringstream ss(line);std::string word;
    while(std::getline(ss,word,'|'))fields.push_back(word);
    ck(col>=0 && static_cast<std::size_t>(col)<fields.size(),"field missing");
    fields.at(static_cast<std::size_t>(col))=value;
    std::string updated;
    for(auto &f:fields){if(!updated.empty())updated+='|';updated+=f;}
    return text.substr(0,found)+updated+text.substr(end);
}
static bool rejected(const std::string& raw){
    try{auto world=Simulation::from_snapshot(raw);(void)world;return false;}
    catch(const std::exception&){return true;}
}
int main(){
    const auto path=(std::filesystem::temp_directory_path()/"sg_q2_integrity.db").string();
    std::filesystem::remove(path);
    Simulation simulation(32);
    const auto home=simulation.add_colony_ship(7,{0,0},100,10);
    const auto ferry=simulation.add_colony_ship(7,{0,0},100,3);
    simulation.enable_individual_workforce();simulation.upgrade_assignment_snapshot_format();
    std::vector<std::uint64_t> manifest{1,11,12,13};
    {
        PersistentWorld world(path,std::move(simulation));
        ck(world.submit_host_passenger_action(action(501,1,Action::HostEmbarkPassengers,ferry,home,700,{1})),"embark");
        world.step();
        auto destruction=action(502,2,Action::HostDestroyPassengerFerry,ferry,ferry,700,manifest);
        auto pre=world.world().canonical_state();
        bool threw=false;
        try{world.submit_host_passenger_action(destruction,FailPoint::AfterReceiptWrite);}
        catch(const std::exception&){threw=true;}
        ck(threw && world.world().canonical_state()==pre,"partial submission leaked state");
        ck(world.submit_host_passenger_action(destruction),"failed submission cannot retry same ID");
        pre=world.world().canonical_state();
        threw=false;
        try{world.step(FailPoint::AfterSnapshotWrite);}catch(const std::exception&){threw=true;}
        ck(threw && world.world().canonical_state()==pre,"failed execution leaked state");
    }
    {
        PersistentWorld world(path,Simulation(1));
        ck(world.world().voyage_status(700)==1,"rolled-back execution committed");
        world.step();
        ck(world.world().voyage_status(700)==6 && world.world().npc_casualties().size()==4,"pending fatal event did not execute exactly once");
        const auto raw=world.world().canonical_state();
        ck(!rejected(raw),"valid fatal snapshot refused");
        ck(rejected(alter(raw,"voyage|700|",4,"1")),"revived voyage accepted");
        ck(rejected(alter(raw,"voyage|700|",7,"888888")),"invented incident receipt accepted");
        ck(rejected(alter(raw,"deceased|1|",8,"999999")),"fake casualty assignment accepted");
        ck(rejected(alter(raw,"deceased|1|",10,"777777")),"wrong death command accepted");
        ck(rejected(alter(raw,"deceased|1|",1,"99")),"rewritten casualty identity accepted");
        ck(rejected(alter(raw,"deceased|1|",11,"500")),"false death ferry accepted");
        ck(rejected(alter(raw,"colony|1|",2,"10")),"invented living crew accepted");
        ck(rejected(alter(raw,"entity|2|",3,"0")),"wreck revived as operational ship");
        const auto erased=raw.find("deceased|1|");ck(erased!=std::string::npos,"death record absent");
        const auto end=raw.find('\n',erased);
        ck(rejected(raw.substr(0,erased)+raw.substr(end+1)),"missing passenger death record accepted");
        // Native crew member is not a voyage passenger. This must still refuse
        // deletion of the death ledger because its physical death was recorded.
        const auto cut=raw.find("deceased|11|");
        ck(cut!=std::string::npos,"crew tombstone absent");
        const auto cut_end=raw.find('\n',cut);
        ck(rejected(raw.substr(0,cut)+raw.substr(cut_end+1)),"missing native crew death record accepted");
    }
    // Deliberately bypass immutable physical-ledger triggers in a test-only copy.
    // Opening the forged DB must independently reconstruct and reject its facts.
    const auto forged=path+".physical_forged";
    std::filesystem::remove(forged);
    std::filesystem::copy_file(path,forged);
    sqlite3* db=nullptr;
    ck(sqlite3_open(forged.c_str(),&db)==SQLITE_OK,"open test corruption database");
    char* error=nullptr;
    const int rc=sqlite3_exec(db,
      "DROP TRIGGER physical_no_update; "
      "UPDATE physical_incidents SET detail='FORGED' WHERE incident_id LIKE 'death:%';",
      nullptr,nullptr,&error);
    if(error)sqlite3_free(error);
    sqlite3_close(db);
    ck(rc==SQLITE_OK,"test physical corruption setup failed");
    bool bad_rejected=false;
    try{PersistentWorld w(forged,Simulation(1));(void)w;}catch(const std::exception&){bad_rejected=true;}
    ck(bad_rejected,"forged casualty ledger accepted");
    std::filesystem::remove(forged);
    // Corrupt historical command content, not the current snapshot: replay
    // from genesis must reject instead of trusting a seemingly valid latest save.
    const auto broken=path+".journal_forged";
    const auto recovered=path+".recovered";
    std::filesystem::remove(broken);std::filesystem::remove(recovered);
    std::filesystem::copy_file(path,broken);
    ck(sqlite3_open(broken.c_str(),&db)==SQLITE_OK,"open journal corruption database");
    error=nullptr;
    const int rc2=sqlite3_exec(db,
      "UPDATE operation_log SET command='FORGED' "
      "WHERE generation=(SELECT MAX(generation) FROM operation_log WHERE kind=1);",
      nullptr,nullptr,&error);
    if(error)sqlite3_free(error);
    sqlite3_close(db);
    ck(rc2==SQLITE_OK,"test journal corruption setup failed");
    bad_rejected=false;
    try{PersistentWorld::recover_copy(broken,recovered);}catch(const std::exception&){bad_rejected=true;}
    ck(bad_rejected,"corrupted death journal replay accepted");
    std::filesystem::remove(broken);std::filesystem::remove(recovered);
    std::filesystem::remove(path);
    std::cout<<"PASS v36 rollback/retry, durable pending event, passenger/crew/custody/wreck corruption refusal\n";
}
