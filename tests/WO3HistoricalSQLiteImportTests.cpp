// WO3-F: positive replay from a genuine vintage native SQLite journal.
// The compiler-repaired pre-v109 kernel (2026-11-10) did NOT provide a persisted host operation
// creating a legal PlanetarySite home. Its v86 migration retains the original
// ship home for actual surface visitors. Do not forge a v86 planetary address.
//
// We test the real v85 SQLite snapshot emitted by the unmodified historical
// kernel (with observation-only original test instrumentation) and the
// original v85->v86 in-memory migration. Modern v109+ real planetary settlement
// is independently accepted by WO3 housing/utility tests.
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
static void require(bool ok,const char* why) {
    if(!ok)throw std::runtime_error(why);
}
int main(int argc,char** argv) {
 try {
    require(argc==2,"usage: source-produced historical v85 SQLite required");
    const fs::path source=argv[1];
    require(fs::is_regular_file(source),"historical native SQLite source missing");
    const auto tmp=fs::temp_directory_path()/
        ("wo3_real_v85_"+std::to_string(std::chrono::steady_clock::now().
                                            time_since_epoch().count()));
    fs::create_directories(tmp);
    struct Cleanup {fs::path path; ~Cleanup(){
        std::error_code ec;fs::remove_all(path,ec);
    }} cleanup{tmp};
    const auto recovered_source=(tmp/"original_recovered.sqlite").string();
    // This must validate the authentic genesis, unmodified original operations,
    // journal, every checkpoint hash, resource ledger and final recorded state.
    PersistentWorld::recover_copy(source.string(),recovered_source);
    std::string original_state;
    std::uint64_t original_hash=0;
    std::int64_t original_matter=0,original_ore=0;
    std::size_t original_people=0;
    std::map<std::uint64_t,transport::Endpoint> original_residences;
    std::size_t physically_landed=0;
    {
        PersistentWorld history(recovered_source,Simulation(613));
        const auto& old=history.world();
        original_state=old.canonical_state();
        require(original_state.rfind("galaxy-kernel-v85|",0)==0,
                "historical saved state is not actual vintage v85");
        require(old.tick()>0 && history.generation()>2 &&
                old.accepted_command_count()>0,
                "vintage world lacks real native original command history");
        original_hash=old.state_hash();
        original_matter=old.total_material_kg();
        original_ore=old.total_ore();
        original_people=old.npc_records().size();
        require(original_people>0 && !old.planetary_sites().empty(),
                "vintage world has no physical NPC or planet");
        for(const auto& [id,n]:old.npc_records()){
            const auto legal=n.legal_residence();
            require(legal.kind==transport::LocationKind::ColonyShip &&
                    legal.id==n.home_ship,
                    "historical native producer unexpectedly assigned planet legal home");
            require(original_residences.emplace(id,legal).second,
                    "duplicate original NPC identity");
            for(const auto& [key,site]:old.planetary_sites()){
                (void)key;
                if(n.location==site.surface_location) ++physically_landed;
            }
        }
        require(physically_landed>0,
                "vintage source lacks physically landed original NPC");
        // The genuine v85->v86 migration is an in-memory explicit source API.
        // It does not create an imaginary historical SQLite operation.
        auto upgraded=Simulation::from_snapshot(original_state);
        upgraded.upgrade_legal_residence_snapshot_format();
        require(upgraded.canonical_state().rfind("galaxy-kernel-v86|",0)==0,
                "genuine v86 legacy upgrade did not set snapshot version");
        require(upgraded.total_ore()==original_ore &&
                upgraded.total_material_kg()==original_matter &&
                upgraded.npc_records().size()==original_people &&
                upgraded.assess_npc_residence().reconciled,
                "genuine v86 legacy upgrade changed people or finite matter");
        for(const auto& [id,expected]:original_residences){
            const auto it=upgraded.npc_records().find(id);
            require(it!=upgraded.npc_records().end() &&
                    it->second.legal_residence()==expected,
                    "genuine v86 native migration fabricated planetary residency");
        }
        require(Simulation::from_snapshot(upgraded.canonical_state()).
                    canonical_state()==upgraded.canonical_state(),
                "genuine v86 native upgraded snapshot failed roundtrip");
        // Even in the authentic v86 in-memory format, a powerful host
        // cannot declare planet legal addresses the native producer never made.
        const auto v86_hash=upgraded.state_hash();
        require(!upgraded.host_import_legacy_planetary_residences(
                    Simulation::kServerAuthority,0x1A091700000001ULL) &&
                upgraded.state_hash()==v86_hash &&
                !upgraded.legacy_planetary_residence_import(),
                "genuine v86 upgrade allowed an invented planetary migration");
        // No owner/host can import a planetary legal address that the vintage
        // producer never granted. These denials MUST NOT append a generation.
        constexpr std::uint64_t request=0x1A091700000001ULL;
        const auto generation=history.generation();
        require(!history.host_import_legacy_planetary_residences(7,request),
                "ordinary user obtained host import authority");
        require(!history.host_import_legacy_planetary_residences(
                    Simulation::kServerAuthority,request),
                "unsupported pre-v109 planetary address was invented");
        require(history.generation()==generation &&
                history.world().state_hash()==original_hash &&
                !history.world().legacy_planetary_residence_import(),
                "invalid historical import mutated original world");
    }
    // Fresh independent copy/restart uses the recorded source, never a
    // patched snapshot or a fabricated genesis. Original person identity,
    // legally distinct address, and all mass must agree byte-for-byte.
    const auto independent=(tmp/"independent_recovery.sqlite").string();
    PersistentWorld::recover_copy(recovered_source,independent);
    {
        PersistentWorld again(independent,Simulation(613));
        const auto& original=again.world();
        require(original.canonical_state()==original_state &&
                original.state_hash()==original_hash &&
                original.total_material_kg()==original_matter &&
                original.total_ore()==original_ore &&
                original.npc_records().size()==original_people &&
                !original.legacy_planetary_residence_import(),
                "independent vintage SQLite recovery changed original custody");
        for(const auto& [id,expected]:original_residences){
            const auto it=original.npc_records().find(id);
            require(it!=original.npc_records().end() &&
                    it->second.legal_residence()==expected,
                    "legacy citizen legal address changed on restart");
        }
    }
    std::cout<<"PASS WO3-F real source-produced historical v85 SQLite, "
                "native original v86 legal-home upgrade, no forged planet "
                "residence, conserved matter, original custody and restart\n";
    return 0;
 }catch(const std::exception& e){
    std::cerr<<"WO3-F historical native-source verification FAIL: "
             <<e.what()<<'\n';
    return 1;
 }
}
