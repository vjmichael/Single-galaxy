#include "galaxy/Persistence.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <sstream>
#include <sqlite3.h>
using namespace galaxy;
static void ck(bool good,const char* what){if(!good)throw std::runtime_error(what);}
static Command cmd(std::uint64_t id,std::uint64_t tick,Action kind,EntityId from,EntityId to,
                   std::uint64_t voyage,std::vector<std::uint64_t> people,std::int64_t rescued=0){
    Command c{};c.id=id;c.execute_tick=tick;c.issuer=Simulation::kServerAuthority;
    c.actor=from;c.target=to;c.action=kind;c.transport_id=voyage;
    c.passenger_ids=std::move(people);c.amount=rescued;return c;
}
static bool bad_snapshot(const std::string& state){
    try{(void)Simulation::from_snapshot(state);return false;}catch(const std::exception&){return true;}
}
static std::string replace_field(const std::string& raw,const std::string& start,std::size_t field,const std::string& value){
    auto pos=raw.find(start);ck(pos!=std::string::npos,"source record not found");
    const auto end=raw.find('\n',pos);ck(end!=std::string::npos,"unterminated record");
    std::istringstream row(raw.substr(pos,end-pos));std::string part;
    std::vector<std::string> fields;
    while(std::getline(row,part,'|'))fields.push_back(part);
    ck(field<fields.size(),"field outside record");fields.at(field)=value;
    std::string replacement;
    for(const auto& s:fields){if(!replacement.empty())replacement+='|';replacement+=s;}
    return raw.substr(0,pos)+replacement+raw.substr(end);
}
int main(){
    const auto path=(std::filesystem::temp_directory_path()/"sg_demog05q3_integrity.db").string();
    std::filesystem::remove(path);
    Simulation sim(18);
    const auto home=sim.add_colony_ship(7,{0,0},100,20);
    const auto ferry=sim.add_colony_ship(7,{0,0},100,6);
    const auto rescue=sim.add_colony_ship(7,{0,0},100,8);
    sim.enable_individual_workforce();sim.upgrade_assignment_snapshot_format();
    auto incident=cmd(701,2,Action::HostResolveMixedFerryLoss,ferry,rescue,900,{1,2,3,21,22,23,24,25,26},2);
    std::string saved;
    {
        PersistentWorld w(path,std::move(sim));
        ck(w.submit_host_passenger_action(cmd(600,1,Action::HostEmbarkPassengers,ferry,home,900,{1,2,3})),"initial boarding");
        w.step();
        saved=w.world().canonical_state();
        bool threw=false;
        try{(void)w.submit_host_passenger_action(incident,FailPoint::AfterReceiptWrite);}catch(const std::exception&){threw=true;}
        ck(threw && saved==w.world().canonical_state(),"submission rollback failed");
        ck(w.submit_host_passenger_action(incident),"retry rejected");
        const auto pending=w.world().canonical_state();
        threw=false;
        try{(void)w.step(FailPoint::AfterSnapshotWrite);}catch(const std::exception&){threw=true;}
        ck(threw && pending==w.world().canonical_state(),"mixed execution rollback failed");
    }
    {
        PersistentWorld w(path,Simulation(111));
        ck(w.world().voyage_status(900)==1,"failed commit persisted");
        w.step();
        ck(w.world().voyage_status(900)==6 && w.world().voyage_status(701)==1,"durable mixed incident missing");
        const auto raw=w.world().canonical_state();
        ck(!bad_snapshot(raw),"correct snapshot rejected");
        ck(bad_snapshot(replace_field(raw,"voyage|900|",10,"0")),"lost survivor rescue link accepted");
        ck(bad_snapshot(replace_field(raw,"voyage|900|",6,"0")),"lost EMS destination accepted");
        ck(bad_snapshot(replace_field(raw,"voyage|701|",2,std::to_string(ferry))),"rescue pointed to wreck");
        ck(bad_snapshot(replace_field(raw,"deceased|3|",10,"700")),"forged fatal command accepted");
        ck(bad_snapshot(replace_field(raw,"colony|1|",2,"20")),"population inflation accepted");
        auto removed=raw.find("deceased|3|");ck(removed!=std::string::npos,"no casualty tombstone");
        const auto end=raw.find('\n',removed);
        ck(bad_snapshot(raw.substr(0,removed)+raw.substr(end+1)),"missing casualty tombstone accepted");
        const auto rescue_rec=raw.find("voyage|701|");ck(rescue_rec!=std::string::npos,"rescue receipt missing");
        const auto rescue_end=raw.find('\n',rescue_rec);
        ck(bad_snapshot(raw.substr(0,rescue_rec)+raw.substr(rescue_end+1)),"missing rescue voyage accepted");
    }
    const auto forged=path+".forged";
    std::filesystem::remove(forged);std::filesystem::copy_file(path,forged);
    sqlite3* db=nullptr;ck(sqlite3_open(forged.c_str(),&db)==SQLITE_OK,"forged DB open");
    char* error=nullptr;
    const auto rc=sqlite3_exec(db,"DROP TRIGGER physical_no_update; UPDATE physical_incidents SET detail='FAKE' WHERE incident_id LIKE 'death:%';",nullptr,nullptr,&error);
    if(error)sqlite3_free(error);sqlite3_close(db);ck(rc==SQLITE_OK,"prepare DB fraud");
    bool rejected=false;try{PersistentWorld tampered(forged,Simulation(1));(void)tampered;}catch(const std::exception&){rejected=true;}
    ck(rejected,"tampered incident facts accepted");std::filesystem::remove(forged);
    // The independent genesis reconstruction must reject an altered v37
    // survivor/fatality partition in the historical command envelope.
    const auto journal_forged=path+".journal_forged";
    const auto refused_output=path+".refused";
    std::filesystem::remove(journal_forged);std::filesystem::remove(refused_output);
    std::filesystem::copy_file(path,journal_forged);
    ck(sqlite3_open(journal_forged.c_str(),&db)==SQLITE_OK,"journal forgery DB open");
    error=nullptr;
    const auto rc2=sqlite3_exec(db,
        "UPDATE operation_log SET command='FORGED-RESCUE-PARTITION' "
        "WHERE kind=1 AND command LIKE '701 %';",nullptr,nullptr,&error);
    if(error)sqlite3_free(error);sqlite3_close(db);
    ck(rc2==SQLITE_OK,"create forged EMS journal");
    rejected=false;
    try{PersistentWorld::recover_copy(journal_forged,refused_output);}catch(const std::exception&){rejected=true;}
    ck(rejected && !std::filesystem::exists(refused_output),
       "tampered EMS command accepted or left partial recovery DB");
    std::filesystem::remove(journal_forged);
    const auto replay=path+".genesis";std::filesystem::remove(replay);
    PersistentWorld::recover_copy(path,replay);
    {
        PersistentWorld live(path,Simulation(1)),regenerated(replay,Simulation(1));
        ck(live.world().canonical_state()==regenerated.world().canonical_state(),"genesis divergence");
    }
    std::filesystem::remove(path);std::filesystem::remove(replay);
    std::cout<<"PASS v37 rollback/retry, missing survivor/death provenance refusal, physical-incident tamper refusal, full genesis replay\n";
}
