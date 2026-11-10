// WO3-F: only a genuine archived v86 original SQLite world may be imported.
// The SHA-256/source-binary proof is checked by the Python runner BEFORE
// this test. No post-genesis Simulation is ever used as SQLite genesis.
#include "galaxy/Persistence.hpp"
#include "galaxy/WO3ColonyOperations.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
using namespace galaxy;
namespace fs=std::filesystem;
static void demand(bool ok,const char* why){
    if(!ok)throw std::runtime_error(why);
}
int main(int argc,char** argv){
 try {
    demand(argc==2,"usage: genuine legacy v86 SQLite source required");
    const fs::path source=argv[1];
    demand(fs::is_regular_file(source),"archived v86 SQLite source missing");
    const auto temp=fs::temp_directory_path()/
        ("wo3_authentic_"+std::to_string(std::chrono::steady_clock::now().
                                             time_since_epoch().count()));
    fs::create_directories(temp);
    struct Cleanup {fs::path p;~Cleanup(){
        std::error_code ec;fs::remove_all(p,ec);
    }} cleanup{temp};
    const auto before=(temp/"original_recovered.sqlite").string();
    // Immutable source must not be edited; all work happens in a recovered
    // independent database after original journal/checkpoint verification.
    PersistentWorld::recover_copy(source.string(),before);
    std::uint64_t original_hash=0,original_matter=0,original_ore=0,original_people=0;
    std::map<std::uint64_t,std::uint64_t> legacy_addresses;
    std::map<std::uint64_t,std::pair<transport::LocationKind,std::uint64_t>> original_residences;
    {
        PersistentWorld history(before,Simulation(613));
        const auto& old=history.world();
        demand(old.canonical_state().rfind("galaxy-kernel-v86|",0)==0,
               "archived source was not genuine v86 native format");
        demand(old.tick()>0 && history.generation()>2,
               "historical source has no actual native command history");
        original_hash=old.state_hash();
        original_matter=old.total_material_kg();
        original_ore=old.total_ore();
        original_people=old.npc_records().size();
        for(const auto& [id,person]:old.npc_records()){
            const auto address=person.legal_residence();
            demand(original_residences.emplace(id,
                       std::make_pair(address.kind,address.id)).second,
                   "duplicate original NPC identity");
            if(address.kind!=transport::LocationKind::PlanetarySite)continue;
            bool matched=false;
            for(const auto& [key,site]:old.planetary_sites()){
                (void)key;
                if(site.surface_location==address.id &&
                   site.source_ship==person.home_ship &&
                   site.spaceport_phase==3)
                    matched=true;
            }
            demand(matched,"archived legal address has no authentic surface");
            demand(legacy_addresses.emplace(id,address.id).second,
                   "duplicate historical resident identity");
        }
        demand(!legacy_addresses.empty(),
               "archived v86 world contains no original surface legal residents");
        constexpr std::uint64_t request=0x1A091700000001ULL;
        demand(!history.host_import_legacy_planetary_residences(7,request),
               "unauthorized host import accepted");
        demand(history.world().state_hash()==original_hash,
               "refused import changed world");
        bool injected=false;
        try {
            (void)history.host_import_legacy_planetary_residences(
                Simulation::kServerAuthority,request,FailPoint::AfterSnapshotWrite);
        }catch(const std::exception&){injected=true;}
        demand(injected && history.world().state_hash()==original_hash,
               "failed SQLite write did not roll back original world");
        demand(history.host_import_legacy_planetary_residences(
                Simulation::kServerAuthority,request),
               "authentic historical import was denied");
        demand(!history.host_import_legacy_planetary_residences(
                Simulation::kServerAuthority,request),
               "one-use import accepted replay");
        const auto& imported=history.world().legacy_planetary_residence_import();
        demand(imported.has_value() && imported->source_version==86 &&
               imported->source_hash==original_hash &&
               imported->host_request_id==request &&
               imported->legal_addresses==legacy_addresses,
               "import changed historical legal identities or provenance");
        // Ore alone misses supplies, original shuttle holds and other matter.
        demand(history.world().total_material_kg()==original_matter &&
               history.world().total_ore()==original_ore &&
               history.world().npc_records().size()==original_people,
               "historical import changed conserved matter or people");
        for(const auto& [id,expected]:original_residences){
            const auto it=history.world().npc_records().find(id);
            demand(it!=history.world().npc_records().end() &&
                   it->second.legal_residence().kind==expected.first &&
                   it->second.legal_residence().id==expected.second,
                   "historical import moved an original NPC");
        }
        demand(history.world().canonical_state().rfind("galaxy-kernel-v115|",0)==0,
               "historical import did not produce versioned v115 state");
    }
    const auto recovered=(temp/"after_original_import.sqlite").string();
    PersistentWorld::recover_copy(before,recovered);
    {
        PersistentWorld history(recovered,Simulation(613));
        const auto& imported=history.world().legacy_planetary_residence_import();
        demand(imported && imported->source_hash==original_hash &&
               imported->legal_addresses==legacy_addresses &&
               history.world().total_material_kg()==original_matter &&
               history.world().total_ore()==original_ore &&
               history.world().npc_records().size()==original_people,
               "independent SQLite restart lost historical matter or legal-address proof");
        for(const auto& [id,expected]:original_residences){
            const auto it=history.world().npc_records().find(id);
            demand(it!=history.world().npc_records().end() &&
                   it->second.legal_residence().kind==expected.first &&
                   it->second.legal_residence().id==expected.second,
                   "independent SQLite restart changed original NPC residence");
        }
    }
    std::cout<<"PASS WO3-F authentic v86 source journal, one-use legal import, "
                "rollback, mass, original people and independent SQLite recovery\n";
    return 0;
 }catch(const std::exception& e){
    std::cerr<<"WO3-F authentic historical provenance FAIL: "<<e.what()<<'\n';
    return 1;
 }
}
