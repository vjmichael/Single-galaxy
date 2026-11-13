// S6-H4: original historical/current SQLite WAL, journal and identity recovery.
// A historical file MUST originate from the pinned native source verifier.
#include "galaxy/Persistence.hpp"
#include <sqlite3.h>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
using namespace galaxy;
namespace fs=std::filesystem;
#define CHECK(x) do {if(!(x))throw std::runtime_error("S6-H4 line "+std::to_string(__LINE__)+": " #x);}while(false)
namespace {
struct Cleanup {fs::path path;~Cleanup(){std::error_code e;fs::remove_all(path,e);}};
struct Db {
 sqlite3* db=nullptr;
 explicit Db(const std::string& path){
  if(sqlite3_open_v2(path.c_str(),&db,SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK){
    if(db)sqlite3_close_v2(db);
    throw std::runtime_error("SQLite H4 original recovery file open failed");
  }
  sqlite3_busy_timeout(db,5000);
 }
 ~Db(){if(db)sqlite3_close_v2(db);}
 void sql(const char* command){
  char* error=nullptr;const int rc=sqlite3_exec(db,command,nullptr,nullptr,&error);
  if(rc!=SQLITE_OK){
   const std::string message=error?error:sqlite3_errmsg(db);sqlite3_free(error);
   throw std::runtime_error("S6-H4 SQL: "+message);
  }
 }
 std::int64_t count(const char* command){
  sqlite3_stmt* q=nullptr;
  if(sqlite3_prepare_v2(db,command,-1,&q,nullptr)!=SQLITE_OK)
   throw std::runtime_error("S6-H4 count prepare failed");
  const int rc=sqlite3_step(q);const auto value=sqlite3_column_int64(q,0);
  sqlite3_finalize(q);CHECK(rc==SQLITE_ROW);return value;
 }
 void integrity(){
  sqlite3_stmt* q=nullptr;CHECK(sqlite3_prepare_v2(db,"PRAGMA integrity_check",-1,&q,nullptr)==SQLITE_OK);
  const int rc=sqlite3_step(q);const auto* answer=sqlite3_column_text(q,0);
  const bool good=rc==SQLITE_ROW && answer &&
      std::strcmp(reinterpret_cast<const char*>(answer),"ok")==0;
  const int end=sqlite3_step(q);sqlite3_finalize(q);CHECK(good && end==SQLITE_DONE);
 }
};
template<class F>void rejects(F&& fun){
 bool refused=false;try{fun();}catch(const std::exception&){refused=true;}CHECK(refused);
}
struct State {
 std::string canonical;std::uint64_t generation=0,hash=0,tick=0;
 std::int64_t ore=0,matter=0,credits=0;
 std::set<EntityId> entities,ships;
 std::map<EntityId,std::uint32_t> systems;
 std::map<std::uint64_t,transport::Endpoint> people;
};
State inspect(const std::string& path){
 PersistentWorld w(path,Simulation(0xA04));
 const auto& s=w.world();State r{};
 r.canonical=s.canonical_state();r.generation=w.generation();
 r.hash=s.state_hash();r.tick=s.tick();
 r.ore=s.total_ore();r.matter=s.total_material_kg();
 r.credits=s.total_credits();
 for(const auto& [id,e]:s.entities()){
  CHECK(r.entities.insert(id).second);
  if(e.type==EntityType::Ship){
   CHECK(r.ships.insert(id).second);r.systems.emplace(id,e.system_id);
  }
 }
 for(const auto& [id,n]:s.npc_records())
  CHECK(r.people.emplace(id,n.legal_residence()).second);
 CHECK(Simulation::from_snapshot(r.canonical).canonical_state()==r.canonical);
 return r;
}
void same(const State& a,const State& b){
 CHECK(a.canonical==b.canonical && a.generation==b.generation &&
       a.hash==b.hash && a.tick==b.tick);
 CHECK(a.ore==b.ore && a.matter==b.matter && a.credits==b.credits);
 CHECK(a.entities==b.entities && a.ships==b.ships &&
       a.people==b.people && a.systems==b.systems);
}
void audit(const fs::path& temp,const std::string& source,bool old){
 const auto baseline=inspect(source);CHECK(!baseline.ships.empty());
 if(old){
  CHECK(baseline.canonical.rfind("galaxy-kernel-v85|",0)==0);
  CHECK(!baseline.people.empty());
  auto upgraded=Simulation::from_snapshot(baseline.canonical);
  upgraded.upgrade_legal_residence_snapshot_format();
  CHECK(upgraded.canonical_state().rfind("galaxy-kernel-v86|",0)==0);
  CHECK(upgraded.total_ore()==baseline.ore &&
        upgraded.total_material_kg()==baseline.matter);
  CHECK(upgraded.npc_records().size()==baseline.people.size());
  for(const auto& [id,home]:baseline.people){
   auto found=upgraded.npc_records().find(id);
   CHECK(found!=upgraded.npc_records().end() &&
         found->second.legal_residence()==home);
   CHECK(home.kind==transport::LocationKind::ColonyShip);
  }
 }
 const auto work=(temp/"working.sqlite").string();
 PersistentWorld::recover_copy(source,work);same(baseline,inspect(work));
 {
  Db writer(work);writer.sql("PRAGMA journal_mode=WAL");
  writer.sql("CREATE TABLE s6h4_wal_audit(receipt INTEGER PRIMARY KEY,kind TEXT NOT NULL)");
  writer.sql("BEGIN IMMEDIATE");
  writer.sql("INSERT INTO s6h4_wal_audit VALUES(1,'committed')");
  writer.sql("COMMIT");writer.sql("BEGIN IMMEDIATE");
  writer.sql("INSERT INTO s6h4_wal_audit VALUES(2,'uncommitted')");
  CHECK(writer.count("SELECT COUNT(*) FROM s6h4_wal_audit")==2);
  const auto recovered=(temp/"hot.sqlite").string();
  PersistentWorld::recover_copy(work,recovered);
  same(baseline,inspect(recovered));
  Db original(recovered);original.integrity();
  CHECK(original.count("SELECT COUNT(*) FROM s6h4_wal_audit")==1);
  CHECK(original.count("SELECT COUNT(*) FROM s6h4_wal_audit WHERE receipt=1 AND kind='committed'")==1);
  rejects([&]{PersistentWorld::recover_copy(work,recovered);});
  writer.sql("ROLLBACK");
  CHECK(writer.count("SELECT COUNT(*) FROM s6h4_wal_audit")==1);
 }
 same(baseline,inspect(work));
 const auto corrupt=(temp/"corrupt.sqlite").string();
 PersistentWorld::recover_copy(work,corrupt);
 {
  Db altered(corrupt);
  altered.sql("UPDATE snapshots SET checksum='0' WHERE generation=(SELECT MAX(generation) FROM snapshots)");
 }
 const auto refused=(temp/"refused.sqlite").string();
 rejects([&]{PersistentWorld::recover_copy(corrupt,refused);});
 CHECK(!fs::exists(refused));
 rejects([&]{(void)inspect(corrupt);});
 const auto verified=(temp/"verified.sqlite").string();
 PersistentWorld::recover_copy(work,verified);same(baseline,inspect(verified));
 rejects([&]{PersistentWorld::recover_copy(work,verified);});
 same(baseline,inspect(source));
}
}
int main(int argc,char** argv){
 try{
  CHECK(argc==2 && argv[1] &&
       (std::string(argv[1])=="--current" || fs::is_regular_file(argv[1])));
  const auto temp=fs::temp_directory_path()/("s6h4_"+std::to_string(
       std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(temp);Cleanup cleanup{temp};
  if(std::string(argv[1])!="--current")audit(temp,argv[1],true);
  else{
   const auto name=(temp/"current.sqlite").string();
   {
    Simulation s(0xBEE34);
    const auto ship=s.add_colony_ship(7,{0,0},100,8);
    s.provision_colony_genesis(ship,1000,1000,1000,1000,1000);
    s.add_asteroid({1000,0},300);
    PersistentWorld w(name,std::move(s));
    Command c{};c.id=7799001;c.execute_tick=1;
    c.issuer=7;c.actor=ship;c.action=Action::Thrust;c.thrust_x=1;
    CHECK(w.submit(c));w.step();CHECK(w.world().has_command_id(c.id));
    const auto before=w.world().canonical_state();
    const auto generation=w.generation();
    rejects([&]{w.step(FailPoint::AfterSnapshotWrite);});
    CHECK(w.generation()==generation && w.world().canonical_state()==before);
   }
   audit(temp,name,false);
  }
  std::cout<<"S6-H4 PASS native original WAL, committed-only recovery, "
              "canonical identity, invalid checksum refusal\n";
  return 0;
 }catch(const std::exception& e){
  std::cerr<<"S6-H4 FAIL "<<e.what()<<'\n';return 1;
 }
}
