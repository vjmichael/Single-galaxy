#include "galaxy/Persistence.hpp"
#include <algorithm>
#include <sqlite3.h>

#include <filesystem>
#include <cerrno>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <share.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
#include <limits>
#include <sstream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace galaxy {
namespace {
[[noreturn]] void db_error(sqlite3* db, const std::string& why) {
    throw std::runtime_error("SQLite " + why + ": " + sqlite3_errmsg(db));
}
void run(sqlite3* db, const char* sql) {
    char* error = nullptr;
    const int code = sqlite3_exec(db, sql, nullptr, nullptr, &error);
    if (code != SQLITE_OK) {
        const std::string message = error ? error : sqlite3_errmsg(db);
        sqlite3_free(error);
        throw std::runtime_error("SQLite statement failed: " + message);
    }
}
class Statement {
public:
    Statement(sqlite3* db, const char* sql) : db_(db) {
        if (sqlite3_prepare_v2(db, sql, -1, &s_, nullptr) != SQLITE_OK) db_error(db, "prepare");
    }
    ~Statement() { sqlite3_finalize(s_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    void i64(int index, std::int64_t value) {
        if (sqlite3_bind_int64(s_, index, value) != SQLITE_OK) db_error(db_, "bind integer");
    }
    void text(int index, const std::string& value) {
        if (sqlite3_bind_text(s_, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT) != SQLITE_OK)
            db_error(db_, "bind text");
    }
    void blob(int index, const std::string& value) {
        if (sqlite3_bind_blob(s_, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT) != SQLITE_OK)
            db_error(db_, "bind blob");
    }
    void execute() {
        if (sqlite3_step(s_) != SQLITE_DONE) db_error(db_, "execute");
    }
    bool row() {
        auto code = sqlite3_step(s_);
        if (code == SQLITE_ROW) return true;
        if (code == SQLITE_DONE) return false;
        db_error(db_, "read");
    }
    sqlite3_stmt* get() { return s_; }
private:
    sqlite3* db_;
    sqlite3_stmt* s_ = nullptr;
};

std::uint64_t parse_u64(const unsigned char* p) {
    if (!p) throw std::runtime_error("SQLite: null u64 value");
    const std::string s(reinterpret_cast<const char*>(p));
    if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos)
        throw std::runtime_error("SQLite: invalid u64 text");
    std::size_t used = 0;
    const auto n = std::stoull(s, &used);
    if (used != s.size()) throw std::runtime_error("SQLite: invalid u64 suffix");
    return n;
}
// Deterministic text form for operations committed after the v2 migration.
std::string command_record(const Command& c) {
    std::ostringstream out;
    out << c.id << ' ' << c.execute_tick << ' ' << c.sequence << ' ' << c.issuer << ' '
        << c.actor << ' ' << c.target << ' ' << static_cast<int>(c.action) << ' '
        << c.thrust_x << ' ' << c.thrust_y << ' ' << c.amount;
    return out.str();
}
bool is_life_action(Action a) {
    return a>=Action::PlantCrops && a<=Action::LoadLifeWater;
}
std::int64_t life_transfer_amount(const Command& c){
    switch(c.action){
      case Action::PlantCrops:return 4*c.amount;
      case Action::HarvestCrops:return 2*c.amount;
      case Action::RecycleWater:return 9*c.amount/10;
      case Action::RecycleCompost:return c.amount;
      case Action::ProduceOxygen:return 8*c.amount;
      case Action::LoadLifeWater:return 1000*c.amount;
      default:return -1;
    }
}
std::string life_ledger_source(const Command& c){
    return "life:"+std::to_string(c.actor)+":"+std::to_string(static_cast<int>(c.action));
}
std::string life_ledger_destination(const Command& c){
    return "life:"+std::to_string(c.actor)+":products";
}
Command decode_command(const std::string& record) {
    Command c{};
    int action = -1;
    std::istringstream in(record);
    if (!(in >> c.id >> c.execute_tick >> c.sequence >> c.issuer >> c.actor
          >> c.target >> action >> c.thrust_x >> c.thrust_y >> c.amount))
        throw std::runtime_error("recovery: invalid command journal record");
    std::string extra;
    if (in >> extra || action < 0 || action > static_cast<int>(Action::HostCrewLoss) || c.id == 0 || c.issuer == 0)
        throw std::runtime_error("recovery: invalid command journal values");
    c.action = static_cast<Action>(action);
    if (command_record(c) != record) throw std::runtime_error("recovery: noncanonical command journal");
    return c;
}
void quick_integrity(sqlite3* db) {
    Statement q(db, "PRAGMA integrity_check");
    if (!q.row() || sqlite3_column_type(q.get(), 0) != SQLITE_TEXT ||
        std::string(reinterpret_cast<const char*>(sqlite3_column_text(q.get(), 0))) != "ok" || q.row())
        throw std::runtime_error("SQLite integrity_check failed; source must be preserved");
}
// Deterministic, non-cryptographic change detector, complemented by strict
// cross-checks against the independent command journal and world snapshot.
std::string historical_digest(const std::string& id, std::uint64_t generation,
                              const Event& ev, const std::string& command) {
    const std::string record = id + "|" + std::to_string(generation) + "|" +
        std::to_string(ev.tick) + "|" + command + "|" +
        std::to_string(static_cast<int>(ev.result)) + "|" + std::to_string(ev.transferred);
    std::uint64_t value = 14695981039346656037ULL;
    for (unsigned char byte : record) { value ^= byte; value *= 1099511628211ULL; }
    return std::to_string(value);
}
std::string physical_digest(const PhysicalIncident& e) {
    // Non-cryptographic corruption detector; strict reconciliation with
    // independent resource/jump ledgers and simulation activity is mandatory.
    const auto value=e.incident_id+"|"+std::to_string(e.generation)+"|"+
        std::to_string(e.tick)+"|"+std::to_string(e.command_id)+"|"+
        std::to_string(static_cast<int>(e.kind))+"|"+e.source+"|"+
        e.destination+"|"+std::to_string(e.amount)+"|"+e.detail;
    std::uint64_t digest=14695981039346656037ULL;
    for(const unsigned char byte:value){digest^=byte;digest*=1099511628211ULL;}
    return std::to_string(digest);
}
std::string sqlite_str(sqlite3_stmt* q,int column){
    const auto* text=sqlite3_column_text(q,column);
    if(!text || sqlite3_column_type(q,column)!=SQLITE_TEXT)
        throw std::runtime_error("SQLite: missing/invalid physical field");
    return reinterpret_cast<const char*>(text);
}
std::uint64_t latest_generation(sqlite3* db) {
    Statement q(db, "SELECT COALESCE(MAX(generation),0) FROM snapshots");
    if (!q.row()) throw std::runtime_error("SQLite: missing generation result");
    return static_cast<std::uint64_t>(sqlite3_column_int64(q.get(), 0));
}
}

class PersistentWorld::Store {
public:
    explicit Store(const std::string& path) {
        if (path.empty()) throw std::invalid_argument("SQLite filename required");
        if (sqlite3_open_v2(path.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
            const std::string error = db_ ? sqlite3_errmsg(db_) : "open failed";
            sqlite3_close(db_); db_ = nullptr;
            throw std::runtime_error("SQLite open: " + error);
        }
        try {
        sqlite3_busy_timeout(db_, 5000);
        run(db_, "PRAGMA foreign_keys=ON");
        run(db_, "PRAGMA journal_mode=WAL");
        run(db_, "PRAGMA synchronous=FULL");
        Statement version(db_, "PRAGMA user_version");
        if (!version.row()) throw std::runtime_error("SQLite: no schema version");
        const int v = sqlite3_column_int(version.get(), 0);
        if (v < 0 || v > 6) throw std::runtime_error("SQLite: incompatible schema version");
        if (v == 0) {
            run(db_, "BEGIN IMMEDIATE");
            try {
                run(db_, "CREATE TABLE IF NOT EXISTS snapshots ("
                         "generation INTEGER PRIMARY KEY CHECK(generation>0),"
                         "format INTEGER NOT NULL CHECK(format=1),"
                         "state BLOB NOT NULL, checksum TEXT NOT NULL, ore_total INTEGER NOT NULL CHECK(ore_total>=0))");
                run(db_, "CREATE TABLE IF NOT EXISTS world_meta ("
                         "singleton INTEGER PRIMARY KEY CHECK(singleton=1), resource_origin INTEGER NOT NULL CHECK(resource_origin>=0))");
                run(db_, "CREATE TABLE IF NOT EXISTS receipts ("
                         "id TEXT PRIMARY KEY, accepted_generation INTEGER NOT NULL,"
                         "execution_tick INTEGER, result INTEGER, transferred INTEGER,"
                         "CHECK(transferred IS NULL OR transferred>=0))");
                run(db_, "CREATE TABLE IF NOT EXISTS resource_ledger ("
                         "id INTEGER PRIMARY KEY AUTOINCREMENT, generation INTEGER NOT NULL,"
                         "tick INTEGER NOT NULL, command_id TEXT UNIQUE, source_id TEXT, destination_id TEXT,"
                         "amount INTEGER NOT NULL CHECK(amount>=0), reason TEXT NOT NULL)");
                run(db_, "PRAGMA user_version=1");
                run(db_, "COMMIT");
            } catch (...) { sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr); throw; }
        }
        if (v <= 1) {
            // One atomic, non-destructive v1 -> v2 upgrade. Pre-upgrade operations
            // cannot be reconstructed; only later v2 operations receive a journal.
            run(db_, "BEGIN IMMEDIATE");
            try {
                run(db_, "CREATE TABLE operation_log ("
                         "generation INTEGER PRIMARY KEY CHECK(generation>0),"
                         "kind INTEGER NOT NULL CHECK(kind BETWEEN 0 AND 2),"
                         "command TEXT, state_hash TEXT NOT NULL, tick INTEGER NOT NULL,"
                         "CHECK((kind=1 AND command IS NOT NULL) OR (kind!=1 AND command IS NULL)))");
                run(db_, "PRAGMA user_version=2");
                run(db_, "COMMIT");
            } catch (...) { sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr); throw; }
        }
        if (v <= 2) {
            const auto has_column = [&](const char* table,const char* name) {
                Statement info(db_, (std::string("PRAGMA table_info(")+table+")").c_str());
                while (info.row()) if (std::string(reinterpret_cast<const char*>(sqlite3_column_text(info.get(),1)))==name) return true;
                return false;
            };
            run(db_, "BEGIN IMMEDIATE");
            try {
                if (!has_column("world_meta","credit_origin"))
                    run(db_, "ALTER TABLE world_meta ADD COLUMN credit_origin INTEGER NOT NULL DEFAULT 0 CHECK(credit_origin>=0)");
                if (!has_column("snapshots","credit_total"))
                    run(db_, "ALTER TABLE snapshots ADD COLUMN credit_total INTEGER NOT NULL DEFAULT 0 CHECK(credit_total>=0)");
                run(db_, "CREATE TABLE IF NOT EXISTS credit_ledger (command_id TEXT PRIMARY KEY, tick INTEGER NOT NULL, station_id TEXT NOT NULL, owner_id TEXT NOT NULL, ore_units INTEGER NOT NULL CHECK(ore_units>0), unit_price INTEGER NOT NULL CHECK(unit_price>0), credit_amount INTEGER NOT NULL CHECK(credit_amount>0))");
                run(db_, "PRAGMA user_version=3");
                run(db_, "COMMIT");
            } catch (...) { sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr); throw; }
        }
        if (v <= 3) {
            run(db_, "BEGIN IMMEDIATE");
            try {
                // A jump's departure and arrival are accountable independent
                // records, committed in the same SQLite transaction as the ship.
                run(db_, "CREATE TABLE IF NOT EXISTS jump_ledger ("
                         "command_id TEXT PRIMARY KEY, ship_id TEXT NOT NULL,"
                         "gate_id TEXT NOT NULL, source_system INTEGER NOT NULL CHECK(source_system>0),"
                         "dest_system INTEGER NOT NULL CHECK(dest_system>0),"
                         "cargo_at_departure INTEGER NOT NULL CHECK(cargo_at_departure>=0),"
                         "accepted_tick INTEGER NOT NULL CHECK(accepted_tick>0),"
                         "completed_tick INTEGER CHECK(completed_tick>accepted_tick))");
                run(db_, "CREATE UNIQUE INDEX IF NOT EXISTS one_pending_jump_per_ship "
                         "ON jump_ledger(ship_id) WHERE completed_tick IS NULL");
                run(db_, "PRAGMA user_version=4");
                run(db_, "COMMIT");
            } catch (...) { sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr); throw; }
        }
        if (v <= 4) {
            // Existing v4 worlds retain all their snapshots/receipts. Earlier
            // events are not silently invented; new events start at this boundary.
            run(db_, "BEGIN IMMEDIATE");
            try {
                const auto baseline = latest_generation(db_);
                run(db_, "CREATE TABLE IF NOT EXISTS historical_meta ("
                         "singleton INTEGER PRIMARY KEY CHECK(singleton=1),"
                         "start_generation INTEGER NOT NULL CHECK(start_generation>=0))");
                run(db_, "CREATE TABLE IF NOT EXISTS historical_events ("
                         "event_id TEXT PRIMARY KEY, command_id TEXT NOT NULL UNIQUE,"
                         "generation INTEGER NOT NULL CHECK(generation>0),"
                         "tick INTEGER NOT NULL CHECK(tick>=0),"
                         "actor_id TEXT NOT NULL, target_id TEXT NOT NULL,"
                         "action INTEGER NOT NULL CHECK(action>=0),"
                         "outcome INTEGER NOT NULL CHECK(outcome>=0),"
                         "transferred INTEGER NOT NULL CHECK(transferred>=0),"
                         "command TEXT NOT NULL, digest TEXT NOT NULL)");
                run(db_, "CREATE INDEX IF NOT EXISTS historical_by_generation ON historical_events(generation)");
                run(db_, "CREATE TRIGGER IF NOT EXISTS historical_no_update BEFORE UPDATE ON historical_events "
                         "BEGIN SELECT RAISE(ABORT, 'historical events are immutable'); END");
                run(db_, "CREATE TRIGGER IF NOT EXISTS historical_no_delete BEFORE DELETE ON historical_events "
                         "BEGIN SELECT RAISE(ABORT, 'historical events are immutable'); END");
                Statement boundary(db_, "INSERT INTO historical_meta(singleton,start_generation) VALUES(1,?)");
                boundary.i64(1, static_cast<std::int64_t>(baseline)); boundary.execute();
                run(db_, "PRAGMA user_version=5");
                run(db_, "COMMIT");
            } catch (...) { sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr); throw; }
        }
        if (v <= 5) {
            // Preserve old journals/snapshots. Never backfill legacy physical
            // events from incomplete evidence or claim old casualties.
            run(db_, "BEGIN IMMEDIATE");
            try {
                const auto baseline=latest_generation(db_);
                run(db_, "CREATE TABLE physical_meta(singleton INTEGER PRIMARY KEY CHECK(singleton=1),"
                         "start_generation INTEGER NOT NULL CHECK(start_generation>=0))");
                run(db_, "CREATE TABLE physical_incidents("
                         "incident_id TEXT PRIMARY KEY, generation INTEGER NOT NULL CHECK(generation>0),"
                         "tick INTEGER NOT NULL CHECK(tick>0),command_id TEXT NOT NULL,"
                         "kind INTEGER NOT NULL CHECK(kind BETWEEN 1 AND 5),"
                         "source TEXT NOT NULL,destination TEXT NOT NULL,"
                         "amount INTEGER NOT NULL CHECK(amount>=0),detail TEXT NOT NULL,digest TEXT NOT NULL)");
                run(db_, "CREATE INDEX physical_by_generation ON physical_incidents(generation)");
                run(db_, "CREATE TRIGGER physical_no_update BEFORE UPDATE ON physical_incidents "
                         "BEGIN SELECT RAISE(ABORT,'physical facts are immutable'); END");
                run(db_, "CREATE TRIGGER physical_no_delete BEFORE DELETE ON physical_incidents "
                         "BEGIN SELECT RAISE(ABORT,'physical facts are immutable'); END");
                Statement m(db_,"INSERT INTO physical_meta(singleton,start_generation) VALUES(1,?)");
                m.i64(1,static_cast<std::int64_t>(baseline));m.execute();
                run(db_,"PRAGMA user_version=6");run(db_,"COMMIT");
            } catch (...) {sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);throw;}
        }
        } catch (...) { sqlite3_close_v2(db_); db_ = nullptr; throw; }
    }
    ~Store() { sqlite3_close(db_); }
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;

    bool restore(Simulation& out, std::uint64_t& generation, std::int64_t& origin, std::int64_t& credit_origin) {
        Statement q(db_, "SELECT generation,format,state,checksum,ore_total,credit_total FROM snapshots ORDER BY generation DESC LIMIT 1");
        if (!q.row()) {
            Statement check(db_, "SELECT COUNT(*) FROM receipts");
            if (!check.row() || sqlite3_column_int64(check.get(), 0) != 0)
                throw std::runtime_error("SQLite: receipts exist without a snapshot; failing closed");
            return false;
        }
        generation = static_cast<std::uint64_t>(sqlite3_column_int64(q.get(), 0));
        if (generation == 0 || sqlite3_column_int(q.get(), 1) != 1)
            throw std::runtime_error("SQLite: unsupported snapshot generation or format");
        const auto* data = static_cast<const char*>(sqlite3_column_blob(q.get(), 2));
        const int size = sqlite3_column_bytes(q.get(), 2);
        if (size <= 0 || !data || size > 16 * 1024 * 1024)
            throw std::runtime_error("SQLite: corrupt current snapshot body; refusing rollback");
        const std::string bytes(data, static_cast<std::size_t>(size));
        const auto expected_hash = parse_u64(sqlite3_column_text(q.get(), 3));
        const auto expected_ore = sqlite3_column_int64(q.get(), 4);
        auto restored = Simulation::from_snapshot(bytes);
        if (restored.state_hash() != expected_hash || restored.total_ore() != expected_ore ||
            restored.total_credits() != sqlite3_column_int64(q.get(), 5))
            throw std::runtime_error("SQLite: corrupt current snapshot checksum/ore; failing closed");
        Statement meta(db_, "SELECT resource_origin,credit_origin FROM world_meta WHERE singleton=1");
        if (!meta.row()) throw std::runtime_error("SQLite: missing world origin");
        origin = sqlite3_column_int64(meta.get(), 0);
        credit_origin = sqlite3_column_int64(meta.get(),1);
        if (origin != expected_ore || credit_origin != restored.total_credits())
            throw std::runtime_error("SQLite: resource/credit conservation failure");
        Statement receipts(db_, "SELECT id,execution_tick,result,transferred FROM receipts");
        std::map<std::uint64_t, Event> event_by_id;
        for (const auto& ev : restored.events()) event_by_id.emplace(ev.command_id, ev);
        std::size_t receipt_count = 0;
        while (receipts.row()) {
            ++receipt_count;
            const auto* raw_id = sqlite3_column_text(receipts.get(), 0);
            const auto id = parse_u64(raw_id);
            if (std::to_string(id) != reinterpret_cast<const char*>(raw_id) || !restored.has_command_id(id))
                throw std::runtime_error("SQLite: receipt ID not in snapshot; failing closed");
            const auto event = event_by_id.find(id);
            if (event == event_by_id.end()) {
                if (sqlite3_column_type(receipts.get(), 1) != SQLITE_NULL ||
                    sqlite3_column_type(receipts.get(), 2) != SQLITE_NULL ||
                    sqlite3_column_type(receipts.get(), 3) != SQLITE_NULL)
                    throw std::runtime_error("SQLite: pending receipt incorrectly marked executed");
            } else {
                if (sqlite3_column_type(receipts.get(), 1) == SQLITE_NULL ||
                    sqlite3_column_type(receipts.get(), 2) == SQLITE_NULL ||
                    sqlite3_column_type(receipts.get(), 3) == SQLITE_NULL ||
                    sqlite3_column_int64(receipts.get(), 1) != static_cast<sqlite3_int64>(event->second.tick) ||
                    sqlite3_column_int(receipts.get(), 2) != static_cast<int>(event->second.result) ||
                    sqlite3_column_int64(receipts.get(), 3) != event->second.transferred)
                    throw std::runtime_error("SQLite: execution receipt contradicts snapshot");
            }
        }
        if (receipt_count != restored.accepted_command_count())
            throw std::runtime_error("SQLite: receipt count mismatch; failing closed");
        // Index the command journal once. Source/recipient checks for hangar
        // transfers must use the authenticated issuer and destination station,
        // not merely an amount that happens to conserve ore.
        std::map<std::uint64_t, Command> journal_commands;
        {
            Statement journal(db_, "SELECT command FROM operation_log WHERE kind=1 ORDER BY generation");
            while (journal.row()) {
                const auto* raw = sqlite3_column_text(journal.get(), 0);
                if (!raw) throw std::runtime_error("SQLite: missing command journal entry");
                const auto c = decode_command(reinterpret_cast<const char*>(raw));
                if (!journal_commands.emplace(c.id, c).second || !restored.has_command_id(c.id))
                    throw std::runtime_error("SQLite: duplicate/unknown command in operation journal");
            }
        }
        // GRSM-01 facts must agree with the independent transaction journal,
        // executed receipts and snapshot. A missing record is a hard error,
        // not a story the world is allowed to silently forget.
        std::uint64_t history_start = 0;
        {
            Statement m(db_, "SELECT start_generation FROM historical_meta WHERE singleton=1");
            if (!m.row()) throw std::runtime_error("SQLite: missing history boundary");
            const auto n = sqlite3_column_int64(m.get(), 0);
            if (n < 0 || static_cast<std::uint64_t>(n) > generation || m.row())
                throw std::runtime_error("SQLite: invalid historical migration boundary");
            history_start = static_cast<std::uint64_t>(n);
        }
        std::map<std::uint64_t, std::uint64_t> expected_event_generation;
        {
            Statement steps(db_, "SELECT generation,tick FROM operation_log WHERE kind=2 AND generation>? ORDER BY generation");
            steps.i64(1, static_cast<std::int64_t>(history_start));
            std::map<std::uint64_t,std::uint64_t> tick_generation;
            while (steps.row()) {
                const auto g = sqlite3_column_int64(steps.get(), 0);
                const auto tick = sqlite3_column_int64(steps.get(), 1);
                if (g <= 0 || tick <= 0 || !tick_generation.emplace(static_cast<std::uint64_t>(tick),
                    static_cast<std::uint64_t>(g)).second)
                    throw std::runtime_error("SQLite: duplicate historical step generation");
            }
            for (const auto& [id,ev] : event_by_id) {
                const auto it=tick_generation.find(ev.tick);
                if (it != tick_generation.end()) expected_event_generation.emplace(id,it->second);
            }
        }
        std::size_t historical_rows=0;
        {
            Statement history(db_, "SELECT event_id,command_id,generation,tick,actor_id,target_id,action,"
                         "outcome,transferred,command,digest FROM historical_events ORDER BY generation,command_id");
            while(history.row()) {
                ++historical_rows;
                const auto read_text=[&](int n)->std::string {
                    if (sqlite3_column_type(history.get(), n)!=SQLITE_TEXT)
                        throw std::runtime_error("SQLite: nontext historical field");
                    const auto raw=sqlite3_column_text(history.get(),n);
                    if(!raw) throw std::runtime_error("SQLite: missing historical field");
                    return reinterpret_cast<const char*>(raw);
                };
                const auto id=parse_u64(sqlite3_column_text(history.get(),1));
                const auto g=sqlite3_column_int64(history.get(),2);
                const auto tick=sqlite3_column_int64(history.get(),3);
                const auto actor=parse_u64(sqlite3_column_text(history.get(),4));
                const auto target=parse_u64(sqlite3_column_text(history.get(),5));
                const auto action=sqlite3_column_int(history.get(),6);
                const auto outcome=sqlite3_column_int(history.get(),7);
                const auto transferred=sqlite3_column_int64(history.get(),8);
                const auto serialized=read_text(9);
                const auto entry=expected_event_generation.find(id);
                const auto ev=event_by_id.find(id);
                const auto cmd=journal_commands.find(id);
                if (entry==expected_event_generation.end() || ev==event_by_id.end() ||
                    cmd==journal_commands.end() || g<=0 || static_cast<std::uint64_t>(g)!=entry->second ||
                    tick<0 || static_cast<std::uint64_t>(tick)!=ev->second.tick ||
                    actor!=cmd->second.actor || target!=cmd->second.target ||
                    action!=static_cast<int>(cmd->second.action) ||
                    outcome!=static_cast<int>(ev->second.result) ||
                    transferred!=ev->second.transferred ||
                    read_text(0)!="command:"+std::to_string(id) ||
                    serialized!=command_record(cmd->second) ||
                    read_text(10)!=historical_digest(read_text(0),static_cast<std::uint64_t>(g),ev->second,serialized))
                    throw std::runtime_error("SQLite: historical fact contradicts world/journal");
            }
        }
        if (historical_rows!=expected_event_generation.size())
            throw std::runtime_error("SQLite: missing committed historical facts");
        // The ledger is a second durable accounting record, not a second source
        // of authority. Detect discrepancies before exposing a restored world.
        Statement ledger(db_, "SELECT command_id,tick,amount,reason,source_id,destination_id FROM resource_ledger ORDER BY id");
        bool saw_genesis = false;
        std::size_t transfers = 0;
        std::size_t hangar_transfers = 0;
        std::size_t load_transfers = 0;
        std::size_t sale_transfers = 0;
        std::size_t refine_transfers=0, fabricate_transfers=0, build_transfers=0;
        std::size_t mineral_refine_transfers=0,material_fabricate_transfers=0;
        std::size_t life_transfers=0, farm_builds=0, farm_stock_transfers=0, habitat_builds=0, habitat_transfers=0,utility_builds=0,utility_repairs=0,utility_spare_transfers=0,community_builds=0,community_transfers=0,clinic_builds=0,clinic_transfers=0,reclaimer_builds=0,waterworks_builds=0,waterworks_transfers=0;
        std::size_t auto_transfers = 0;
        std::vector<ActivityEvent> recorded_auto;
        for (const auto& a : restored.activity_events()) {
            if (a.kind == ActivityKind::AutoMiningTransfer) recorded_auto.push_back(a);
        }
        while (ledger.row()) {
            const auto reason_raw = sqlite3_column_text(ledger.get(), 3);
            if (!reason_raw) throw std::runtime_error("SQLite: null ledger reason");
            const std::string reason(reinterpret_cast<const char*>(reason_raw));
            const auto amount = sqlite3_column_int64(ledger.get(), 2);
            if (reason == "initial_world_allocation") {
                if (saw_genesis || sqlite3_column_type(ledger.get(), 0) != SQLITE_NULL ||
                    amount != origin || sqlite3_column_int64(ledger.get(), 1) != 0)
                    throw std::runtime_error("SQLite: malformed genesis ledger entry");
                saw_genesis = true;
            } else if (reason == "mining_transfer") {
                ++transfers;
                const auto id = parse_u64(sqlite3_column_text(ledger.get(), 0));
                const auto it = event_by_id.find(id);
                if (it == event_by_id.end() || it->second.result != Outcome::Applied ||
                    it->second.transferred <= 0 || it->second.transferred != amount ||
                    it->second.tick != static_cast<std::uint64_t>(sqlite3_column_int64(ledger.get(), 1)))
                    throw std::runtime_error("SQLite: ledger transfer contradicts executed event");
                const auto cmd = journal_commands.find(id);
                if (cmd != journal_commands.end() &&
                    (cmd->second.action != Action::Mine ||
                     std::to_string(cmd->second.target) != reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(), 4)) ||
                     std::to_string(cmd->second.actor) != reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(), 5))))
                    throw std::runtime_error("SQLite: mining ledger source or destination mismatch");
            } else if (reason == "hangar_transfer") {
                ++hangar_transfers;
                const auto id = parse_u64(sqlite3_column_text(ledger.get(), 0));
                const auto it = event_by_id.find(id);
                const auto cmd = journal_commands.find(id);
                if (it == event_by_id.end() || it->second.result != Outcome::Applied ||
                    it->second.transferred <= 0 || it->second.transferred != amount ||
                    it->second.tick != static_cast<std::uint64_t>(sqlite3_column_int64(ledger.get(), 1)) ||
                    sqlite3_column_type(ledger.get(), 4) != SQLITE_TEXT ||
                    sqlite3_column_type(ledger.get(), 5) != SQLITE_TEXT ||
                    cmd == journal_commands.end() || cmd->second.action != Action::UnloadOre ||
                    std::to_string(cmd->second.actor) != reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(), 4)) ||
                    ("hangar:" + std::to_string(cmd->second.target) + ":owner:" +
                        std::to_string(cmd->second.issuer)) != reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(), 5)))
                    throw std::runtime_error("SQLite: hangar ledger source/destination or receipt mismatch");
            } else if (reason == "load_transfer" || reason == "sale_transfer") {
                if (reason == "load_transfer") ++load_transfers;
                else ++sale_transfers;
                const auto id = parse_u64(sqlite3_column_text(ledger.get(),0));
                const auto it=event_by_id.find(id); const auto cmd=journal_commands.find(id);
                if(it==event_by_id.end() || cmd==journal_commands.end() ||
                   it->second.result!=Outcome::Applied || it->second.transferred<=0 ||
                   it->second.transferred!=amount ||
                   it->second.tick!=static_cast<std::uint64_t>(sqlite3_column_int64(ledger.get(),1)) ||
                   cmd->second.action!=(reason=="load_transfer" ? Action::LoadOre : Action::SellOre))
                    throw std::runtime_error("SQLite: logistic transfer receipt mismatch");
                const auto hangar_id="hangar:"+std::to_string(cmd->second.target)+":owner:"+std::to_string(cmd->second.issuer);
                const auto market_id="market:"+std::to_string(cmd->second.target);
                const auto src=reason=="load_transfer" ? hangar_id : hangar_id;
                const auto dst=reason=="load_transfer" ? std::to_string(cmd->second.actor) : market_id;
                if(sqlite3_column_type(ledger.get(),4)!=SQLITE_TEXT ||
                   sqlite3_column_type(ledger.get(),5)!=SQLITE_TEXT ||
                   src!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),4)) ||
                   dst!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),5)))
                    throw std::runtime_error("SQLite: logistic transfer destination mismatch");
            } else if(reason=="refining_transfer" || reason=="fabrication_transfer" ||
                      reason=="construction_embodiment"){
                const auto action=reason=="refining_transfer" ? Action::RefineOre :
                                  reason=="fabrication_transfer" ? Action::FabricateModule :
                                  Action::ConstructOutpost;
                if(action==Action::RefineOre)++refine_transfers;
                else if(action==Action::FabricateModule)++fabricate_transfers;
                else ++build_transfers;
                const auto id=parse_u64(sqlite3_column_text(ledger.get(),0));
                const auto ev=event_by_id.find(id); const auto cmd=journal_commands.find(id);
                if(ev==event_by_id.end() || cmd==journal_commands.end() ||
                   cmd->second.action!=action || ev->second.result!=Outcome::Applied ||
                   ev->second.transferred!=amount || amount<=0 ||
                   (action==Action::RefineOre && amount!=cmd->second.amount) ||
                   (action==Action::FabricateModule && amount!=2*cmd->second.amount) ||
                   (action==Action::ConstructOutpost && amount!=8) ||
                   ev->second.tick!=static_cast<std::uint64_t>(sqlite3_column_int64(ledger.get(),1)) ||
                   sqlite3_column_type(ledger.get(),4)!=SQLITE_TEXT ||
                   sqlite3_column_type(ledger.get(),5)!=SQLITE_TEXT ||
                   std::to_string(cmd->second.actor)!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),4)) ||
                   (reason=="refining_transfer" ? "refined:" :
                    reason=="fabrication_transfer" ? "modules:" : "outpost:")+std::to_string(cmd->second.actor)!=
                      reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),5)))
                    throw std::runtime_error("SQLite: colony transformation ledger mismatch");
            } else if (reason=="mineral_refining_kg" || reason=="material_fabrication_t"){
                const bool refine=reason=="mineral_refining_kg";
                if(refine)++mineral_refine_transfers;else ++material_fabricate_transfers;
                const auto id=parse_u64(sqlite3_column_text(ledger.get(),0));
                const auto ev=event_by_id.find(id);const auto cmd=journal_commands.find(id);
                if(ev==event_by_id.end() || cmd==journal_commands.end() ||
                   cmd->second.action!=(refine?Action::RefineMineral:Action::FabricateMaterialModule) ||
                   ev->second.result!=Outcome::Applied || ev->second.transferred!=amount || amount<=0 ||
                   amount!=(refine?cmd->second.amount:cmd->second.amount*2) ||
                   ev->second.tick!=static_cast<std::uint64_t>(sqlite3_column_int64(ledger.get(),1)) ||
                   sqlite3_column_type(ledger.get(),4)!=SQLITE_TEXT ||
                   sqlite3_column_type(ledger.get(),5)!=SQLITE_TEXT)
                    throw std::runtime_error("SQLite: mineral/material transformation receipt mismatch");
                const auto src=(refine?"mineral:":"good:")+std::to_string(cmd->second.actor)+":"+std::to_string(cmd->second.target);
                const auto dst=(refine?"goods:":"modules:")+std::to_string(cmd->second.actor);
                if(src!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),4)) ||
                   dst!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),5)))
                    throw std::runtime_error("SQLite: mineral/material ledger endpoints mismatch");
            } else if (reason=="life_support_transform_kg") {
                ++life_transfers;
                const auto id=parse_u64(sqlite3_column_text(ledger.get(),0));
                const auto ev=event_by_id.find(id); const auto cmd=journal_commands.find(id);
                if(ev==event_by_id.end() || cmd==journal_commands.end() ||
                   !is_life_action(cmd->second.action) ||
                   ev->second.result!=Outcome::Applied || ev->second.transferred!=amount ||
                   amount<=0 || amount!=life_transfer_amount(cmd->second) ||
                   ev->second.tick!=static_cast<std::uint64_t>(sqlite3_column_int64(ledger.get(),1)) ||
                   sqlite3_column_type(ledger.get(),4)!=SQLITE_TEXT ||
                   sqlite3_column_type(ledger.get(),5)!=SQLITE_TEXT ||
                   life_ledger_source(cmd->second)!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),4)) ||
                   life_ledger_destination(cmd->second)!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),5)))
                    throw std::runtime_error("SQLite: life-support operation ledger mismatch");
            } else if(reason=="farm_construction_t" || reason=="farm_stock_transfer_kg"){
                const bool build=reason=="farm_construction_t";
                if(build)++farm_builds;else ++farm_stock_transfers;
                const auto id=parse_u64(sqlite3_column_text(ledger.get(),0));
                const auto ev=event_by_id.find(id);const auto cmd=journal_commands.find(id);
                if(ev==event_by_id.end() || cmd==journal_commands.end() ||
                   cmd->second.action!=(build?Action::ConstructFarm:Action::TransferFarmStock) ||
                   ev->second.result!=Outcome::Applied || ev->second.transferred!=amount ||
                   amount!=(build?4:std::abs(cmd->second.amount)) ||
                   ev->second.tick!=static_cast<std::uint64_t>(sqlite3_column_int64(ledger.get(),1)) ||
                   sqlite3_column_type(ledger.get(),4)!=SQLITE_TEXT ||
                   sqlite3_column_type(ledger.get(),5)!=SQLITE_TEXT)
                    throw std::runtime_error("SQLite: farm resource receipt mismatch");
                const auto source=(build?"modules:":cmd->second.amount>0?"life:":"farm:")+
                    std::to_string(build?cmd->second.actor:cmd->second.amount>0?cmd->second.actor:cmd->second.target);
                const auto dest=(build?"farm:":cmd->second.amount>0?"farm:":"life:")+
                    std::to_string(build?cmd->second.target:cmd->second.amount>0?cmd->second.target:cmd->second.actor);
                if(source!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),4)) ||
                   dest!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),5)))
                    throw std::runtime_error("SQLite: farm resource ledger endpoints mismatch");
            } else if (reason=="habitat_construction_t" || reason=="habitat_stock_transfer_kg"){
                const bool build=reason=="habitat_construction_t";
                if(build)++habitat_builds;else ++habitat_transfers;
                const auto id=parse_u64(sqlite3_column_text(ledger.get(),0));
                const auto ev=event_by_id.find(id);const auto cmd=journal_commands.find(id);
                if(ev==event_by_id.end()||cmd==journal_commands.end()||
                   cmd->second.action!=(build?Action::ConstructHabitat:Action::TransferHabitatStock)||
                   ev->second.result!=Outcome::Applied||ev->second.transferred!=amount||
                   amount!=(build?4:std::abs(cmd->second.amount))||
                   ev->second.tick!=static_cast<std::uint64_t>(sqlite3_column_int64(ledger.get(),1))||
                   sqlite3_column_type(ledger.get(),4)!=SQLITE_TEXT||
                   sqlite3_column_type(ledger.get(),5)!=SQLITE_TEXT)
                    throw std::runtime_error("SQLite: habitat resource receipt mismatch");
                const auto source=(build?"modules:":cmd->second.amount>0?"life:":"habitat:")+
                    std::to_string(build?cmd->second.actor:cmd->second.amount>0?cmd->second.actor:cmd->second.target);
                const auto dest=(build?"habitat:":cmd->second.amount>0?"habitat:":"life:")+
                    std::to_string(build?cmd->second.target:cmd->second.amount>0?cmd->second.target:cmd->second.actor);
                if(source!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),4))||
                   dest!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),5)))
                    throw std::runtime_error("SQLite: habitat ledger endpoints mismatch");
            } else if(reason=="utility_construction_t" || reason=="utility_repair_t"){
                const bool build=reason=="utility_construction_t";
                if(build)++utility_builds;else ++utility_repairs;
                const auto id=parse_u64(sqlite3_column_text(ledger.get(),0));
                const auto ev=event_by_id.find(id);const auto cmd=journal_commands.find(id);
                if(ev==event_by_id.end()||cmd==journal_commands.end()||
                   cmd->second.action!=(build?Action::ConstructUtility:Action::RepairUtility)||
                   ev->second.result!=Outcome::Applied||ev->second.transferred!=amount||
                   amount!=(build?4:2)||cmd->second.amount!=1||
                   ev->second.tick!=static_cast<std::uint64_t>(sqlite3_column_int64(ledger.get(),1))||
                   sqlite3_column_type(ledger.get(),4)!=SQLITE_TEXT||
                   sqlite3_column_type(ledger.get(),5)!=SQLITE_TEXT)
                    throw std::runtime_error("SQLite: utility operation receipt mismatch");
                const auto source="modules:"+std::to_string(cmd->second.actor);
                const auto dest="utility:"+std::to_string(cmd->second.target);
                if(source!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),4))||
                   dest!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),5)))
                    throw std::runtime_error("SQLite: utility resource ledger endpoints mismatch");
            } else if(reason=="utility_spare_transfer_t"){
                ++utility_spare_transfers;
                const auto id=parse_u64(sqlite3_column_text(ledger.get(),0));
                const auto ev=event_by_id.find(id);const auto cmd=journal_commands.find(id);
                if(ev==event_by_id.end() || cmd==journal_commands.end() ||
                   cmd->second.action!=Action::TransferUtilitySpares ||
                   ev->second.result!=Outcome::Applied ||
                   ev->second.tick!=static_cast<std::uint64_t>(sqlite3_column_int64(ledger.get(),1)) ||
                   amount!=2*std::abs(cmd->second.amount) || ev->second.transferred!=amount ||
                   sqlite3_column_type(ledger.get(),4)!=SQLITE_TEXT ||
                   sqlite3_column_type(ledger.get(),5)!=SQLITE_TEXT)
                    throw std::runtime_error("SQLite: utility spare receipt mismatch");
                const auto source=(cmd->second.amount>0?"modules:":"utility-spares:")+
                    std::to_string(cmd->second.amount>0?cmd->second.actor:cmd->second.target);
                const auto dest=(cmd->second.amount>0?"utility-spares:":"modules:")+
                    std::to_string(cmd->second.amount>0?cmd->second.target:cmd->second.actor);
                if(source!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),4)) ||
                   dest!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),5)))
                    throw std::runtime_error("SQLite: utility spare endpoints mismatch");
            } else if(reason=="community_construction_t" || reason=="community_stock_transfer_kg"){
                const bool build=reason=="community_construction_t";
                if(build)++community_builds;else ++community_transfers;
                const auto id=parse_u64(sqlite3_column_text(ledger.get(),0));
                const auto ev=event_by_id.find(id);const auto cmd=journal_commands.find(id);
                if(ev==event_by_id.end() || cmd==journal_commands.end() ||
                   cmd->second.action!=(build?Action::ConstructCommunity:Action::TransferCommunityStock) ||
                   ev->second.result!=Outcome::Applied || ev->second.transferred!=amount ||
                   amount!=(build?4:std::abs(cmd->second.amount)) ||
                   ev->second.tick!=static_cast<std::uint64_t>(sqlite3_column_int64(ledger.get(),1)) ||
                   sqlite3_column_type(ledger.get(),4)!=SQLITE_TEXT ||
                   sqlite3_column_type(ledger.get(),5)!=SQLITE_TEXT)
                    throw std::runtime_error("SQLite: community resource receipt mismatch");
                const auto source=(build?"modules:":cmd->second.amount>0?"life:":"community:")+
                    std::to_string(build?cmd->second.actor:cmd->second.amount>0?cmd->second.actor:cmd->second.target);
                const auto dest=(build?"community:":cmd->second.amount>0?"community:":"life:")+
                    std::to_string(build?cmd->second.target:cmd->second.amount>0?cmd->second.target:cmd->second.actor);
                if(source!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),4)) ||
                   dest!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),5)))
                    throw std::runtime_error("SQLite: community ledger endpoints mismatch");
            } else if(reason=="clinic_construction_t" || reason=="clinic_stock_transfer_kg"){
                const bool build=reason=="clinic_construction_t";
                if(build)++clinic_builds;else ++clinic_transfers;
                const auto id=parse_u64(sqlite3_column_text(ledger.get(),0));
                const auto ev=event_by_id.find(id);const auto cmd=journal_commands.find(id);
                if(ev==event_by_id.end() || cmd==journal_commands.end() ||
                   cmd->second.action!=(build?Action::ConstructClinic:Action::TransferClinicStock) ||
                   ev->second.result!=Outcome::Applied || ev->second.transferred!=amount ||
                   amount!=(build?4:std::abs(cmd->second.amount)) ||
                   ev->second.tick!=static_cast<std::uint64_t>(sqlite3_column_int64(ledger.get(),1)) ||
                   sqlite3_column_type(ledger.get(),4)!=SQLITE_TEXT ||
                   sqlite3_column_type(ledger.get(),5)!=SQLITE_TEXT)
                    throw std::runtime_error("SQLite: clinic resource receipt mismatch");
                const auto source=(build?"modules:":cmd->second.amount>0?"life:":"clinic:")+
                    std::to_string(build?cmd->second.actor:cmd->second.amount>0?cmd->second.actor:cmd->second.target);
                const auto dest=(build?"clinic:":cmd->second.amount>0?"clinic:":"life:")+
                    std::to_string(build?cmd->second.target:cmd->second.amount>0?cmd->second.target:cmd->second.actor);
                if(source!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),4)) ||
                   dest!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),5)))
                    throw std::runtime_error("SQLite: clinic ledger endpoints mismatch");
            } else if(reason=="waterworks_construction_t" || reason=="waterworks_feed_transfer_kg"){
                const bool build=reason=="waterworks_construction_t";
                if(build)++waterworks_builds;else ++waterworks_transfers;
                const auto id=parse_u64(sqlite3_column_text(ledger.get(),0));
                const auto ev=event_by_id.find(id);const auto cmd=journal_commands.find(id);
                if(ev==event_by_id.end() || cmd==journal_commands.end() ||
                   cmd->second.action!=(build?Action::ConstructWaterworks:Action::TransferWaterworksFeed) ||
                   ev->second.result!=Outcome::Applied ||
                   ev->second.transferred!=amount ||
                   amount!=(build?4:std::abs(cmd->second.amount)) ||
                   ev->second.tick!=static_cast<std::uint64_t>(sqlite3_column_int64(ledger.get(),1)) ||
                   sqlite3_column_type(ledger.get(),4)!=SQLITE_TEXT ||
                   sqlite3_column_type(ledger.get(),5)!=SQLITE_TEXT)
                    throw std::runtime_error("SQLite: waterworks receipt mismatch");
                const auto source=(build?"modules:":cmd->second.amount>0?"good-water:":"waterworks-feed:")+
                    std::to_string(build?cmd->second.actor:cmd->second.amount>0?cmd->second.actor:cmd->second.target);
                const auto dest=(build?"waterworks:":cmd->second.amount>0?"waterworks-feed:":"good-water:")+
                    std::to_string(build?cmd->second.target:cmd->second.amount>0?cmd->second.target:cmd->second.actor);
                if(source!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),4)) ||
                   dest!=reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),5)))
                    throw std::runtime_error("SQLite: waterworks receipt endpoints mismatch");
            } else if(reason=="reclaimer_construction_t"){
                ++reclaimer_builds;
                const auto id=parse_u64(sqlite3_column_text(ledger.get(),0));
                const auto ev=event_by_id.find(id);const auto cmd=journal_commands.find(id);
                if(ev==event_by_id.end() || cmd==journal_commands.end() ||
                   cmd->second.action!=Action::ConstructReclaimer ||
                   ev->second.result!=Outcome::Applied || ev->second.transferred!=4 ||
                   amount!=4 ||
                   ev->second.tick!=static_cast<std::uint64_t>(sqlite3_column_int64(ledger.get(),1)) ||
                   sqlite3_column_type(ledger.get(),4)!=SQLITE_TEXT ||
                   sqlite3_column_type(ledger.get(),5)!=SQLITE_TEXT ||
                   std::string(reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),4)))!=
                     "modules:"+std::to_string(cmd->second.actor) ||
                   std::string(reinterpret_cast<const char*>(sqlite3_column_text(ledger.get(),5)))!=
                     "reclaimer:"+std::to_string(cmd->second.target))
                    throw std::runtime_error("SQLite: reclaimer resource receipt mismatch");
            } else if (reason == "automated_mining_transfer") {
                if (auto_transfers >= recorded_auto.size() ||
                    sqlite3_column_type(ledger.get(), 0) != SQLITE_NULL)
                    throw std::runtime_error("SQLite: extra or noncanonical auto transfer");
                const auto& ev = recorded_auto[auto_transfers++];
                if (parse_u64(sqlite3_column_text(ledger.get(), 4)) != ev.target ||
                    parse_u64(sqlite3_column_text(ledger.get(), 5)) != ev.actor ||
                    amount != ev.transferred ||
                    sqlite3_column_int64(ledger.get(), 1) != static_cast<sqlite3_int64>(ev.tick))
                    throw std::runtime_error("SQLite: auto mining amount/tick mismatch");
            } else throw std::runtime_error("SQLite: unrecognized ledger reason");
        }
        if (auto_transfers != recorded_auto.size())
            throw std::runtime_error("SQLite: missing auto mining resource ledger rows");
        std::size_t expected_transfers = 0;
        std::size_t expected_hangar_transfers = 0;
        std::size_t expected_load_transfers = 0;
        std::size_t expected_sale_transfers = 0;
        std::size_t expected_refine=0,expected_fabricate=0,expected_build=0;
        std::size_t expected_mineral_refine=0,expected_material_fabricate=0;
        std::size_t expected_life_transfers=0,expected_farm_builds=0,expected_farm_stock_transfers=0;
        std::size_t expected_habitat_builds=0, expected_habitat_transfers=0;
        std::size_t expected_utility_builds=0,expected_utility_repairs=0,expected_utility_spares=0,expected_community_builds=0,expected_community_transfers=0,expected_clinic_builds=0,expected_clinic_transfers=0,expected_reclaimer_builds=0,expected_waterworks_builds=0,expected_waterworks_transfers=0;

        for (const auto& ev : restored.events()) {
            if (ev.result != Outcome::Applied || ev.transferred <= 0) continue;
            const auto cmd = journal_commands.find(ev.command_id);
            if (cmd == journal_commands.end()) {
                // Pre-v2 snapshots can have valid mining receipts predating the
                // operation journal. No hangar transfer existed before B5.
                ++expected_transfers;
            } else if (cmd->second.action == Action::Mine) ++expected_transfers;
            else if (cmd->second.action == Action::UnloadOre) ++expected_hangar_transfers;
            else if (cmd->second.action == Action::LoadOre) ++expected_load_transfers;
            else if (cmd->second.action == Action::SellOre) ++expected_sale_transfers;
            else if (cmd->second.action == Action::RefineOre) ++expected_refine;
            else if (cmd->second.action == Action::FabricateModule) ++expected_fabricate;
            else if (cmd->second.action == Action::ConstructOutpost) ++expected_build;
            else if (cmd->second.action == Action::RefineMineral) ++expected_mineral_refine;
            else if (cmd->second.action == Action::FabricateMaterialModule) ++expected_material_fabricate;
            else if (is_life_action(cmd->second.action)) ++expected_life_transfers;
            else if (cmd->second.action==Action::ConstructFarm) ++expected_farm_builds;
            else if (cmd->second.action==Action::TransferFarmStock) ++expected_farm_stock_transfers;
            else if (cmd->second.action==Action::ConstructHabitat) ++expected_habitat_builds;
            else if (cmd->second.action==Action::TransferHabitatStock) ++expected_habitat_transfers;
            else if (cmd->second.action==Action::ConstructUtility) ++expected_utility_builds;
            else if (cmd->second.action==Action::RepairUtility) ++expected_utility_repairs;
            else if (cmd->second.action==Action::TransferUtilitySpares) ++expected_utility_spares;
            else if (cmd->second.action==Action::ConstructCommunity) ++expected_community_builds;
            else if (cmd->second.action==Action::TransferCommunityStock) ++expected_community_transfers;
            else if (cmd->second.action==Action::ConstructReclaimer) ++expected_reclaimer_builds;
            else if (cmd->second.action==Action::ConstructWaterworks) ++expected_waterworks_builds;
            else if (cmd->second.action==Action::TransferWaterworksFeed) ++expected_waterworks_transfers;
            else if (cmd->second.action==Action::ConstructClinic) ++expected_clinic_builds;
            else if (cmd->second.action==Action::TransferClinicStock) ++expected_clinic_transfers;
            else throw std::runtime_error("SQLite: resource transfer tied to non-transfer command");
        }
        if (!saw_genesis || transfers != expected_transfers || hangar_transfers != expected_hangar_transfers ||
            load_transfers != expected_load_transfers || sale_transfers != expected_sale_transfers ||
            refine_transfers!=expected_refine || fabricate_transfers!=expected_fabricate ||
            build_transfers!=expected_build ||
            mineral_refine_transfers!=expected_mineral_refine ||
            material_fabricate_transfers!=expected_material_fabricate ||
            life_transfers!=expected_life_transfers ||
            farm_builds!=expected_farm_builds ||
            farm_stock_transfers!=expected_farm_stock_transfers ||
            habitat_builds!=expected_habitat_builds ||
            habitat_transfers!=expected_habitat_transfers ||
            utility_builds!=expected_utility_builds || utility_repairs!=expected_utility_repairs ||
            utility_spare_transfers!=expected_utility_spares ||
            community_builds!=expected_community_builds || community_transfers!=expected_community_transfers ||
            clinic_builds!=expected_clinic_builds || clinic_transfers!=expected_clinic_transfers ||
            reclaimer_builds!=expected_reclaimer_builds ||
            waterworks_builds!=expected_waterworks_builds ||
            waterworks_transfers!=expected_waterworks_transfers)
            throw std::runtime_error("SQLite: ledger event count mismatch");
        std::size_t credit_rows=0;
        Statement cash(db_, "SELECT command_id,tick,station_id,owner_id,ore_units,unit_price,credit_amount FROM credit_ledger");
        while (cash.row()) {
            ++credit_rows;
            const auto id=parse_u64(sqlite3_column_text(cash.get(),0));
            const auto cmd=journal_commands.find(id); const auto it=event_by_id.find(id);
            if (cmd==journal_commands.end() || cmd->second.action!=Action::SellOre ||
                it==event_by_id.end() || it->second.result!=Outcome::Applied ||
                it->second.transferred!=sqlite3_column_int64(cash.get(),4) ||
                it->second.tick!=static_cast<std::uint64_t>(sqlite3_column_int64(cash.get(),1)) ||
                std::to_string(cmd->second.target)!=reinterpret_cast<const char*>(sqlite3_column_text(cash.get(),2)) ||
                std::to_string(cmd->second.issuer)!=reinterpret_cast<const char*>(sqlite3_column_text(cash.get(),3)))
                throw std::runtime_error("SQLite: sale credit journal contradiction");
            const auto market=restored.market(cmd->second.target);
            const auto unit=sqlite3_column_int64(cash.get(),5);
            const auto amount=sqlite3_column_int64(cash.get(),6);
            if (!market || unit!=market->price_per_ore || unit<=0 ||
                it->second.transferred<=0 ||
                it->second.transferred>std::numeric_limits<std::int64_t>::max()/unit ||
                amount!=it->second.transferred*unit)
                throw std::runtime_error("SQLite: sale credit amount or price mismatch");
        }
        if (credit_rows!=expected_sale_transfers)
            throw std::runtime_error("SQLite: missing sale credit journal");
        // B6.2b: independently verify both sides of each inter-system transfer.
        std::size_t successful_jump_events=0,completed_jump_events=0,jump_rows=0;
        for (const auto& ev : restored.events()) {
            const auto cmd=journal_commands.find(ev.command_id);
            if(cmd!=journal_commands.end() && cmd->second.action==Action::RequestJump &&
               ev.result==Outcome::Applied) ++successful_jump_events;
        }
        for(const auto& a:restored.activity_events())if(a.kind==ActivityKind::JumpCompleted)
            ++completed_jump_events;
        Statement jumps(db_,"SELECT command_id,ship_id,gate_id,source_system,dest_system,"
                           "cargo_at_departure,accepted_tick,completed_tick FROM jump_ledger");
        std::size_t completed_rows=0;
        while(jumps.row()){
            ++jump_rows;
            const auto id=parse_u64(sqlite3_column_text(jumps.get(),0));
            const auto ship_id=parse_u64(sqlite3_column_text(jumps.get(),1));
            const auto gate_id=parse_u64(sqlite3_column_text(jumps.get(),2));
            const auto source=static_cast<std::uint64_t>(sqlite3_column_int64(jumps.get(),3));
            const auto destination=static_cast<std::uint64_t>(sqlite3_column_int64(jumps.get(),4));
            const auto cargo=sqlite3_column_int64(jumps.get(),5);
            const auto accepted_tick=static_cast<std::uint64_t>(sqlite3_column_int64(jumps.get(),6));
            const auto completed=sqlite3_column_type(jumps.get(),7)!=SQLITE_NULL;
            const auto completed_tick=completed?static_cast<std::uint64_t>(sqlite3_column_int64(jumps.get(),7)):0;
            const auto cmd=journal_commands.find(id); const auto event=event_by_id.find(id);
            const auto* gate=restored.find(gate_id);
            const auto* ship=restored.find(ship_id);
            if(cmd==journal_commands.end() || cmd->second.action!=Action::RequestJump ||
               cmd->second.actor!=ship_id || cmd->second.target!=gate_id ||
               event==event_by_id.end() || event->second.result!=Outcome::Applied ||
               event->second.tick!=accepted_tick || event->second.transferred!=0 ||
               !gate || gate->type!=EntityType::Stargate || !ship ||
               (ship->type!=EntityType::Ship && (ship->type!=EntityType::Wreck || !completed)) ||
               gate->system_id!=source || gate->gate_destination!=destination ||
               cargo<0 || cargo>ship->cargo_capacity)
                throw std::runtime_error("SQLite: jump departure ledger inconsistent");
            if(completed){
                ++completed_rows;
                if(completed_tick!=accepted_tick+Simulation::kJumpTransitTicks)
                    throw std::runtime_error("SQLite: invalid jump completion time");
                const auto it=std::find_if(restored.activity_events().begin(),restored.activity_events().end(),
                    [&](const ActivityEvent& a){return a.kind==ActivityKind::JumpCompleted &&
                        a.tick==completed_tick && a.actor==ship_id && a.target==gate_id;});
                if(it==restored.activity_events().end())
                    throw std::runtime_error("SQLite: completed jump lacks arrival journal");
            }else if(ship->system_id!=source || ship->jump_state!=ShipJumpState::Jumping ||
                     ship->pending_gate!=gate_id || ship->cargo!=cargo)
                throw std::runtime_error("SQLite: uncompleted jump has no matching vessel");
        }
        if(jump_rows!=successful_jump_events || completed_rows!=completed_jump_events)
            throw std::runtime_error("SQLite: missing or duplicate jump journal entry");
        // GRSM-01b: compare every immutable incident after migration against
        // independent transactional resource/jump journals and activity facts.
        std::uint64_t physical_start=0;
        {
            Statement m(db_,"SELECT start_generation FROM physical_meta WHERE singleton=1");
            if(!m.row())throw std::runtime_error("SQLite: missing physical boundary");
            const auto n=sqlite3_column_int64(m.get(),0);
            if(n<0 || static_cast<std::uint64_t>(n)>generation || m.row())
                throw std::runtime_error("SQLite: invalid physical boundary");
            physical_start=static_cast<std::uint64_t>(n);
        }
        std::map<std::string,PhysicalIncident> expected_physical;
        const auto expect=[&](const PhysicalIncident& p){
            if(p.generation<=physical_start || p.generation>generation || p.tick==0 ||
               !expected_physical.emplace(p.incident_id,p).second)
                throw std::runtime_error("SQLite: duplicate/invalid expected physical fact");
        };
        {
            Statement r(db_,"SELECT id,generation,tick,command_id,source_id,destination_id,amount,reason "
                            "FROM resource_ledger WHERE generation>? AND source_id IS NOT NULL ORDER BY id");
            r.i64(1,static_cast<std::int64_t>(physical_start));
            while(r.row()){
                PhysicalIncident e;
                e.incident_id="cargo:"+std::to_string(sqlite3_column_int64(r.get(),0));
                e.generation=static_cast<std::uint64_t>(sqlite3_column_int64(r.get(),1));
                e.tick=static_cast<std::uint64_t>(sqlite3_column_int64(r.get(),2));
                if(sqlite3_column_type(r.get(),3)!=SQLITE_NULL)e.command_id=parse_u64(sqlite3_column_text(r.get(),3));
                e.kind=PhysicalKind::CargoMovement;
                e.source=sqlite_str(r.get(),4);e.destination=sqlite_str(r.get(),5);
                e.amount=sqlite3_column_int64(r.get(),6);e.detail=sqlite_str(r.get(),7);
                if(e.amount<=0 || e.source.empty() || e.destination.empty())
                    throw std::runtime_error("SQLite: invalid physical cargo reference");
                expect(e);
            }
        }
        // Jumps retain exact ship ID, origin/destination, and departure cargo.
        // Completed journeys are separate facts, never overwritten start facts.
        {
            Statement j(db_,"SELECT j.command_id,j.ship_id,j.gate_id,j.source_system,j.dest_system,"
                "j.cargo_at_departure,j.accepted_tick,j.completed_tick,h.generation "
                "FROM jump_ledger j JOIN historical_events h ON h.command_id=j.command_id "
                "WHERE h.generation>? ORDER BY j.command_id");
            j.i64(1,static_cast<std::int64_t>(physical_start));
            while(j.row()){
                const auto id=parse_u64(sqlite3_column_text(j.get(),0));
                const auto ship=sqlite_str(j.get(),1),gate=sqlite_str(j.get(),2);
                const auto source=std::to_string(sqlite3_column_int64(j.get(),3));
                const auto dest=std::to_string(sqlite3_column_int64(j.get(),4));
                const auto cargo=sqlite3_column_int64(j.get(),5);
                const auto departed=static_cast<std::uint64_t>(sqlite3_column_int64(j.get(),6));
                const auto startgen=static_cast<std::uint64_t>(sqlite3_column_int64(j.get(),8));
                expect(PhysicalIncident{"journey:start:"+std::to_string(id),startgen,departed,id,
                    PhysicalKind::JourneyStarted,source,dest,cargo,ship+":"+gate});
                if(sqlite3_column_type(j.get(),7)!=SQLITE_NULL){
                    const auto arrival=static_cast<std::uint64_t>(sqlite3_column_int64(j.get(),7));
                    Statement step(db_,"SELECT generation FROM operation_log WHERE kind=2 AND tick=?");
                    step.i64(1,static_cast<std::int64_t>(arrival));
                    if(!step.row())throw std::runtime_error("SQLite: arrival missing committed tick");
                    const auto arrgen=static_cast<std::uint64_t>(sqlite3_column_int64(step.get(),0));
                    expect(PhysicalIncident{"journey:end:"+std::to_string(id),arrgen,arrival,id,
                        PhysicalKind::JourneyCompleted,source,dest,cargo,ship+":"+gate});
                }
            }
        }
        for(const auto& a:restored.activity_events()){
            if(a.kind!=ActivityKind::ShipDestroyed && a.kind!=ActivityKind::NpcLost)continue;
            const auto h=expected_event_generation.find(a.start_command_id);
            const auto cmd=journal_commands.find(a.start_command_id);
            if(h==expected_event_generation.end() || cmd==journal_commands.end() ||
               ((a.kind==ActivityKind::ShipDestroyed && cmd->second.action!=Action::HostDestroyShip) ||
                (a.kind==ActivityKind::NpcLost && cmd->second.action!=Action::HostCrewLoss)) ||
               a.tick!=event_by_id.at(a.start_command_id).tick ||
               event_by_id.at(a.start_command_id).result!=Outcome::Applied)
                throw std::runtime_error("SQLite: physical loss lacks executed host action");
            if(h->second<=physical_start)continue;
            expect(PhysicalIncident{(a.kind==ActivityKind::ShipDestroyed?"wreck:":"death:")+
                    std::to_string(a.start_command_id)+(a.kind==ActivityKind::NpcLost?":"+std::to_string(a.target):""),
                h->second,a.tick,a.start_command_id,
                a.kind==ActivityKind::ShipDestroyed?PhysicalKind::ShipDestroyed:PhysicalKind::NpcLost,
                std::to_string(a.actor),std::to_string(a.target),1,
                a.kind==ActivityKind::ShipDestroyed?"wreck_persists":"individual_identity_lost"});
        }
        {
            Statement facts(db_,"SELECT incident_id,generation,tick,command_id,kind,source,destination,amount,detail,digest "
                                "FROM physical_incidents");
            std::size_t count=0;
            while(facts.row()){
                ++count;
                PhysicalIncident e;
                e.incident_id=sqlite_str(facts.get(),0);
                const auto gen=sqlite3_column_int64(facts.get(),1),tick=sqlite3_column_int64(facts.get(),2);
                if(gen<=0 || tick<=0)throw std::runtime_error("SQLite: invalid physical timestamp");
                e.generation=static_cast<std::uint64_t>(gen);e.tick=static_cast<std::uint64_t>(tick);
                e.command_id=parse_u64(sqlite3_column_text(facts.get(),3));
                const auto kind=sqlite3_column_int(facts.get(),4);
                if(kind<1 || kind>5)throw std::runtime_error("SQLite: invalid physical kind");
                e.kind=static_cast<PhysicalKind>(kind);
                e.source=sqlite_str(facts.get(),5);e.destination=sqlite_str(facts.get(),6);
                e.amount=sqlite3_column_int64(facts.get(),7);e.detail=sqlite_str(facts.get(),8);
                const auto it=expected_physical.find(e.incident_id);
                if(it==expected_physical.end() || e.generation!=it->second.generation ||
                   e.tick!=it->second.tick || e.command_id!=it->second.command_id ||
                   e.kind!=it->second.kind || e.source!=it->second.source ||
                   e.destination!=it->second.destination || e.amount!=it->second.amount ||
                   e.detail!=it->second.detail || sqlite_str(facts.get(),9)!=physical_digest(e))
                    throw std::runtime_error("SQLite: physical incident contradicts authoritative source");
            }
            if(count!=expected_physical.size())
                throw std::runtime_error("SQLite: physical incident missing or duplicated");
        }
        out.swap(restored);
        return true;
    }

    void commit(const Simulation& state, std::uint64_t expected_generation, std::int64_t origin, std::int64_t credit_origin,
                const Command* accepted, const std::vector<Event>& new_events,
                const std::map<std::uint64_t, Command>& due_commands,
                const std::vector<ActivityEvent>& activity_delta, FailPoint fail) {
        if (state.total_ore() != origin || state.total_credits() != credit_origin)
            throw std::runtime_error("resource or credit conservation violation");
        const auto bytes = state.canonical_state();
        if (bytes.size() > 16 * 1024 * 1024)
            throw std::runtime_error("snapshot limit exceeded; checkpoint compaction needed");
        run(db_, "BEGIN IMMEDIATE");
        try {
            const auto current = latest_generation(db_);
            if (current != expected_generation)
                throw std::runtime_error("stale world generation; reload before writing");
            if (expected_generation >= static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max()))
                throw std::runtime_error("generation exhausted");
            const auto next = expected_generation + 1;
            if (expected_generation == 0) {
                Statement meta(db_, "INSERT INTO world_meta(singleton,resource_origin,credit_origin) VALUES(1,?,?)");
                meta.i64(1, origin); meta.i64(2,credit_origin); meta.execute();
                Statement ledger(db_, "INSERT INTO resource_ledger(generation,tick,amount,reason) VALUES(?,?,?,?)");
                ledger.i64(1, static_cast<std::int64_t>(next));
                ledger.i64(2, 0); ledger.i64(3, origin); ledger.text(4, "initial_world_allocation"); ledger.execute();
            }
            if (accepted) {
                Statement receipt(db_, "INSERT INTO receipts(id,accepted_generation) VALUES(?,?)");
                receipt.text(1, std::to_string(accepted->id));
                receipt.i64(2, static_cast<std::int64_t>(next));
                receipt.execute();
            }
            for (const auto& ev : new_events) {
                Statement update(db_, "UPDATE receipts SET execution_tick=?,result=?,transferred=? WHERE id=? AND execution_tick IS NULL");
                update.i64(1, static_cast<std::int64_t>(ev.tick));
                update.i64(2, static_cast<int>(ev.result));
                update.i64(3, ev.transferred);
                update.text(4, std::to_string(ev.command_id));
                update.execute();
                if (sqlite3_changes(db_) != 1)
                    throw std::runtime_error("missing/double execution receipt");
                // Same transaction as receipts, physical resource ledgers and
                // snapshot. A rejected action is recorded as rejected, never
                // inferred to have harmed people, ships or cargo.
                const auto due = due_commands.find(ev.command_id);
                if (due==due_commands.end() || due->second.execute_tick!=ev.tick ||
                    ev.transferred<0 || ev.tick!=state.tick())
                    throw std::runtime_error("history: unmatched executed command");
                const auto record=command_record(due->second);
                const auto historical_id="command:"+std::to_string(ev.command_id);
                Statement history(db_, "INSERT INTO historical_events(event_id,command_id,generation,tick,"
                    "actor_id,target_id,action,outcome,transferred,command,digest) VALUES(?,?,?,?,?,?,?,?,?,?,?)");
                history.text(1,historical_id);
                history.text(2,std::to_string(ev.command_id));
                history.i64(3,static_cast<std::int64_t>(next));
                history.i64(4,static_cast<std::int64_t>(ev.tick));
                history.text(5,std::to_string(due->second.actor));
                history.text(6,std::to_string(due->second.target));
                history.i64(7,static_cast<int>(due->second.action));
                history.i64(8,static_cast<int>(ev.result));
                history.i64(9,ev.transferred);
                history.text(10,record);
                history.text(11,historical_digest(historical_id,next,ev,record));
                history.execute();
                if (ev.result == Outcome::Applied && ev.transferred > 0) {
                    const auto cmd = due_commands.find(ev.command_id);
                    if (cmd == due_commands.end() ||
                        (cmd->second.action != Action::Mine && cmd->second.action != Action::UnloadOre &&
                         cmd->second.action != Action::LoadOre && cmd->second.action != Action::SellOre &&
                         cmd->second.action != Action::RefineOre &&
                         cmd->second.action != Action::FabricateModule &&
                         cmd->second.action != Action::ConstructOutpost &&
                         cmd->second.action != Action::RefineMineral &&
                         cmd->second.action != Action::FabricateMaterialModule &&
                         !is_life_action(cmd->second.action) &&
                         cmd->second.action!=Action::ConstructFarm &&
                         cmd->second.action!=Action::TransferFarmStock &&
                         cmd->second.action!=Action::ConstructHabitat &&
                         cmd->second.action!=Action::TransferHabitatStock &&
                         cmd->second.action!=Action::ConstructUtility &&
                         cmd->second.action!=Action::RepairUtility &&
                         cmd->second.action!=Action::TransferUtilitySpares &&
                         cmd->second.action!=Action::ConstructCommunity &&
                         cmd->second.action!=Action::TransferCommunityStock &&
                         cmd->second.action!=Action::ConstructWaterworks &&
                         cmd->second.action!=Action::TransferWaterworksFeed &&
                         cmd->second.action!=Action::ConstructReclaimer &&
                         cmd->second.action!=Action::ConstructClinic &&
                         cmd->second.action!=Action::TransferClinicStock))
                        throw std::runtime_error("unmatched resource transfer event");
                    Statement ledger(db_, "INSERT INTO resource_ledger(generation,tick,command_id,source_id,destination_id,amount,reason) VALUES(?,?,?,?,?,?,?)");
                    ledger.i64(1, static_cast<std::int64_t>(next));
                    ledger.i64(2, static_cast<std::int64_t>(ev.tick));
                    ledger.text(3, std::to_string(ev.command_id));
                    if (cmd->second.action == Action::Mine) {
                        ledger.text(4, std::to_string(cmd->second.target));
                        ledger.text(5, std::to_string(cmd->second.actor));
                        ledger.i64(6, ev.transferred); ledger.text(7, "mining_transfer");
                    } else if(cmd->second.action==Action::RefineMineral ||
                              cmd->second.action==Action::FabricateMaterialModule){
                        const auto refine=cmd->second.action==Action::RefineMineral;
                        ledger.text(4,(refine?"mineral:":"good:")+std::to_string(cmd->second.actor)+":"+std::to_string(cmd->second.target));
                        ledger.text(5,(refine?"goods:":"modules:")+std::to_string(cmd->second.actor));
                        ledger.i64(6,ev.transferred);
                        ledger.text(7,refine?"mineral_refining_kg":"material_fabrication_t");
                    } else if(cmd->second.action==Action::ConstructFarm ||
                              cmd->second.action==Action::TransferFarmStock){
                        const bool build=cmd->second.action==Action::ConstructFarm;
                        const auto& c=cmd->second;
                        if(ev.transferred!=(build?4:std::abs(c.amount)))
                            throw std::runtime_error("farm transfer receipt mismatch");
                        ledger.text(4,(build?"modules:":c.amount>0?"life:":"farm:")+
                            std::to_string(build?c.actor:c.amount>0?c.actor:c.target));
                        ledger.text(5,(build?"farm:":c.amount>0?"farm:":"life:")+
                            std::to_string(build?c.target:c.amount>0?c.target:c.actor));
                        ledger.i64(6,ev.transferred);
                        ledger.text(7,build?"farm_construction_t":"farm_stock_transfer_kg");
                    } else if(cmd->second.action==Action::ConstructHabitat ||
                              cmd->second.action==Action::TransferHabitatStock){
                        const bool build=cmd->second.action==Action::ConstructHabitat;
                        const auto& c=cmd->second;
                        if(ev.transferred!=(build?4:std::abs(c.amount)))
                            throw std::runtime_error("habitat transfer receipt mismatch");
                        ledger.text(4,(build?"modules:":c.amount>0?"life:":"habitat:")+
                            std::to_string(build?c.actor:c.amount>0?c.actor:c.target));
                        ledger.text(5,(build?"habitat:":c.amount>0?"habitat:":"life:")+
                            std::to_string(build?c.target:c.amount>0?c.target:c.actor));
                        ledger.i64(6,ev.transferred);
                        ledger.text(7,build?"habitat_construction_t":"habitat_stock_transfer_kg");
                    } else if(cmd->second.action==Action::ConstructUtility ||
                              cmd->second.action==Action::RepairUtility){
                        const bool build=cmd->second.action==Action::ConstructUtility;
                        if(ev.transferred!=(build?4:2))
                            throw std::runtime_error("utility transfer receipt mismatch");
                        ledger.text(4,"modules:"+std::to_string(cmd->second.actor));
                        ledger.text(5,"utility:"+std::to_string(cmd->second.target));
                        ledger.i64(6,ev.transferred);
                        ledger.text(7,build?"utility_construction_t":"utility_repair_t");
                    } else if(cmd->second.action==Action::TransferUtilitySpares){
                        const auto& c=cmd->second;
                        if(ev.transferred!=2*std::abs(c.amount))
                            throw std::runtime_error("utility spare transfer receipt mismatch");
                        ledger.text(4,(c.amount>0?"modules:":"utility-spares:")+
                            std::to_string(c.amount>0?c.actor:c.target));
                        ledger.text(5,(c.amount>0?"utility-spares:":"modules:")+
                            std::to_string(c.amount>0?c.target:c.actor));
                        ledger.i64(6,ev.transferred);
                        ledger.text(7,"utility_spare_transfer_t");
                    } else if(cmd->second.action==Action::ConstructCommunity ||
                              cmd->second.action==Action::TransferCommunityStock){
                        const bool build=cmd->second.action==Action::ConstructCommunity;
                        const auto& c=cmd->second;
                        if(ev.transferred!=(build?4:std::abs(c.amount)))
                            throw std::runtime_error("community transfer receipt mismatch");
                        ledger.text(4,(build?"modules:":c.amount>0?"life:":"community:")+
                            std::to_string(build?c.actor:c.amount>0?c.actor:c.target));
                        ledger.text(5,(build?"community:":c.amount>0?"community:":"life:")+
                            std::to_string(build?c.target:c.amount>0?c.target:c.actor));
                        ledger.i64(6,ev.transferred);
                        ledger.text(7,build?"community_construction_t":"community_stock_transfer_kg");
                    } else if(cmd->second.action==Action::ConstructWaterworks ||
                              cmd->second.action==Action::TransferWaterworksFeed){
                        const auto& c=cmd->second;
                        const bool build=c.action==Action::ConstructWaterworks;
                        if(ev.transferred!=(build?4:std::abs(c.amount)))
                            throw std::runtime_error("waterworks transfer amount mismatch");
                        ledger.text(4,(build?"modules:":c.amount>0?"good-water:":"waterworks-feed:")+
                            std::to_string(build?c.actor:c.amount>0?c.actor:c.target));
                        ledger.text(5,(build?"waterworks:":c.amount>0?"waterworks-feed:":"good-water:")+
                            std::to_string(build?c.target:c.amount>0?c.target:c.actor));
                        ledger.i64(6,ev.transferred);
                        ledger.text(7,build?"waterworks_construction_t":"waterworks_feed_transfer_kg");
                    } else if(cmd->second.action==Action::ConstructReclaimer){
                        if(ev.transferred!=4)throw std::runtime_error("reclaimer resource receipt mismatch");
                        ledger.text(4,"modules:"+std::to_string(cmd->second.actor));
                        ledger.text(5,"reclaimer:"+std::to_string(cmd->second.target));
                        ledger.i64(6,4);ledger.text(7,"reclaimer_construction_t");
                    } else if(cmd->second.action==Action::ConstructClinic ||
                              cmd->second.action==Action::TransferClinicStock){
                        const bool build=cmd->second.action==Action::ConstructClinic;
                        const auto& c=cmd->second;
                        if(ev.transferred!=(build?4:std::abs(c.amount)))
                            throw std::runtime_error("clinic transfer receipt mismatch");
                        ledger.text(4,(build?"modules:":c.amount>0?"life:":"clinic:")+
                            std::to_string(build?c.actor:c.amount>0?c.actor:c.target));
                        ledger.text(5,(build?"clinic:":c.amount>0?"clinic:":"life:")+
                            std::to_string(build?c.target:c.amount>0?c.target:c.actor));
                        ledger.i64(6,ev.transferred);
                        ledger.text(7,build?"clinic_construction_t":"clinic_stock_transfer_kg");
                    } else if(is_life_action(cmd->second.action)){
                        if(ev.transferred != life_transfer_amount(cmd->second))
                            throw std::runtime_error("life-support transfer receipt mismatch");
                        ledger.text(4,life_ledger_source(cmd->second));
                        ledger.text(5,life_ledger_destination(cmd->second));
                        ledger.i64(6,ev.transferred);
                        ledger.text(7,"life_support_transform_kg");
                    } else if(cmd->second.action==Action::RefineOre ||
                              cmd->second.action==Action::FabricateModule ||
                              cmd->second.action==Action::ConstructOutpost){
                        // Store physical ore-equivalent movements, not module counts.
                        // State hash + invariant prove transformations conserve mass.
                        const auto source=std::to_string(cmd->second.actor);
                        const auto label=cmd->second.action==Action::RefineOre ? "refined" :
                                         cmd->second.action==Action::FabricateModule ? "modules" : "outpost";
                        ledger.text(4,source);
                        ledger.text(5,label+std::string(":")+source);
                        ledger.i64(6,ev.transferred);
                        ledger.text(7,cmd->second.action==Action::RefineOre ? "refining_transfer" :
                                      cmd->second.action==Action::FabricateModule ? "fabrication_transfer" :
                                      "construction_embodiment");
                    } else {
                        const auto hangar="hangar:"+std::to_string(cmd->second.target)+
                            ":owner:"+std::to_string(cmd->second.issuer);
                        if (cmd->second.action == Action::UnloadOre) {
                            ledger.text(4,std::to_string(cmd->second.actor));
                            ledger.text(5,hangar);
                            ledger.text(7,"hangar_transfer");
                        } else if (cmd->second.action == Action::LoadOre) {
                            ledger.text(4,hangar);
                            ledger.text(5,std::to_string(cmd->second.actor));
                            ledger.text(7,"load_transfer");
                        } else {
                            ledger.text(4,hangar);
                            ledger.text(5,"market:"+std::to_string(cmd->second.target));
                            ledger.text(7,"sale_transfer");
                            const auto m=state.market(cmd->second.target);
                            if (!m || m->price_per_ore<=0 ||
                                ev.transferred>std::numeric_limits<std::int64_t>::max()/m->price_per_ore)
                                throw std::runtime_error("invalid sale credit calculation");
                            Statement credit(db_,"INSERT INTO credit_ledger(command_id,tick,station_id,owner_id,ore_units,unit_price,credit_amount) VALUES(?,?,?,?,?,?,?)");
                            credit.text(1,std::to_string(ev.command_id));
                            credit.i64(2,static_cast<std::int64_t>(ev.tick));
                            credit.text(3,std::to_string(cmd->second.target));
                            credit.text(4,std::to_string(cmd->second.issuer));
                            credit.i64(5,ev.transferred);
                            credit.i64(6,m->price_per_ore);
                            credit.i64(7,ev.transferred*m->price_per_ore);
                            credit.execute();
                        }
                        ledger.i64(6, ev.transferred);
                    }
                    ledger.execute();
                }
            }
            for(const auto& ev:new_events){
                if(ev.result!=Outcome::Applied)continue;
                const auto cmd=due_commands.find(ev.command_id);
                if(cmd==due_commands.end() || cmd->second.action!=Action::RequestJump)continue;
                const auto* ship=state.find(cmd->second.actor);
                const auto* gate=state.find(cmd->second.target);
                if(!ship || !gate || ship->type!=EntityType::Ship || gate->type!=EntityType::Stargate ||
                   ship->system_id!=gate->system_id || ship->pending_gate!=gate->id ||
                   ship->jump_state!=ShipJumpState::Jumping || ev.transferred!=0)
                    throw std::runtime_error("invalid jump departure state");
                Statement row(db_,"INSERT INTO jump_ledger(command_id,ship_id,gate_id,"
                                  "source_system,dest_system,cargo_at_departure,accepted_tick)"
                                  " VALUES(?,?,?,?,?,?,?)");
                row.text(1,std::to_string(cmd->second.id));
                row.text(2,std::to_string(ship->id));
                row.text(3,std::to_string(gate->id));
                row.i64(4,ship->system_id);row.i64(5,gate->gate_destination);
                row.i64(6,ship->cargo);row.i64(7,ev.tick);row.execute();
            }
            for (const auto& a : activity_delta) {
                if(a.kind==ActivityKind::JumpCompleted){
                    const auto* ship=state.find(a.actor);
                    const auto* gate=state.find(a.target);
                    if(!ship || !gate || ship->type!=EntityType::Ship || gate->type!=EntityType::Stargate ||
                        ship->system_id!=gate->gate_destination || ship->jump_state!=ShipJumpState::ArrivalCloak)
                        throw std::runtime_error("invalid jump arrival state");
                    Statement complete(db_,"UPDATE jump_ledger SET completed_tick=? "
                                           "WHERE ship_id=? AND gate_id=? AND completed_tick IS NULL "
                                           "AND dest_system=? AND cargo_at_departure=?");
                    complete.i64(1,a.tick);complete.text(2,std::to_string(a.actor));
                    complete.text(3,std::to_string(a.target));
                    complete.i64(4,ship->system_id);complete.i64(5,ship->cargo);
                    complete.execute();
                    if(sqlite3_changes(db_)!=1)
                        throw std::runtime_error("missing or duplicate jump completion");
                }
                if (a.kind != ActivityKind::AutoMiningTransfer) continue;
                if (a.result != Outcome::Applied || a.transferred <= 0 ||
                    a.tick != state.tick() || !state.has_command_id(a.start_command_id))
                    throw std::runtime_error("invalid automated mining transfer");
                Statement ledger(db_, "INSERT INTO resource_ledger(generation,tick,source_id,destination_id,amount,reason) VALUES(?,?,?,?,?,?)");
                ledger.i64(1, static_cast<std::int64_t>(next));
                ledger.i64(2, static_cast<std::int64_t>(a.tick));
                ledger.text(3, std::to_string(a.target));
                ledger.text(4, std::to_string(a.actor));
                ledger.i64(5, a.transferred);
                ledger.text(6, "automated_mining_transfer");
                ledger.execute();
            }
            // Write physical incident rows in the SAME transaction as all
            // sources, receipts, state snapshot and operation journal.
            auto add_physical=[&](const PhysicalIncident& e){
                Statement add(db_,"INSERT INTO physical_incidents(incident_id,generation,tick,command_id,"
                    "kind,source,destination,amount,detail,digest) VALUES(?,?,?,?,?,?,?,?,?,?)");
                add.text(1,e.incident_id);add.i64(2,static_cast<std::int64_t>(e.generation));
                add.i64(3,static_cast<std::int64_t>(e.tick));add.text(4,std::to_string(e.command_id));
                add.i64(5,static_cast<int>(e.kind));add.text(6,e.source);add.text(7,e.destination);
                add.i64(8,e.amount);add.text(9,e.detail);add.text(10,physical_digest(e));add.execute();
            };
            {
                Statement r(db_,"SELECT id,tick,command_id,source_id,destination_id,amount,reason "
                                "FROM resource_ledger WHERE generation=? AND source_id IS NOT NULL ORDER BY id");
                r.i64(1,static_cast<std::int64_t>(next));
                while(r.row()){
                    PhysicalIncident e;
                    e.incident_id="cargo:"+std::to_string(sqlite3_column_int64(r.get(),0));
                    e.generation=next;e.tick=static_cast<std::uint64_t>(sqlite3_column_int64(r.get(),1));
                    if(sqlite3_column_type(r.get(),2)!=SQLITE_NULL)e.command_id=parse_u64(sqlite3_column_text(r.get(),2));
                    e.kind=PhysicalKind::CargoMovement;
                    e.source=sqlite_str(r.get(),3);e.destination=sqlite_str(r.get(),4);
                    e.amount=sqlite3_column_int64(r.get(),5);e.detail=sqlite_str(r.get(),6);
                    add_physical(e);
                }
            }
            for(const auto& ev:new_events){
                const auto cmd=due_commands.find(ev.command_id);
                if(ev.result!=Outcome::Applied || cmd==due_commands.end() ||
                   cmd->second.action!=Action::RequestJump)continue;
                Statement j(db_,"SELECT ship_id,gate_id,source_system,dest_system,cargo_at_departure "
                                "FROM jump_ledger WHERE command_id=?");
                j.text(1,std::to_string(ev.command_id));
                if(!j.row())throw std::runtime_error("history: missing jump start source");
                const auto ship=sqlite_str(j.get(),0),gate=sqlite_str(j.get(),1);
                add_physical(PhysicalIncident{"journey:start:"+std::to_string(ev.command_id),
                    next,ev.tick,ev.command_id,PhysicalKind::JourneyStarted,
                    std::to_string(sqlite3_column_int64(j.get(),2)),
                    std::to_string(sqlite3_column_int64(j.get(),3)),
                    sqlite3_column_int64(j.get(),4),ship+":"+gate});
            }
            for(const auto& a:activity_delta){
                if(a.kind==ActivityKind::JumpCompleted){
                    Statement j(db_,"SELECT command_id,ship_id,gate_id,source_system,dest_system,cargo_at_departure "
                                    "FROM jump_ledger WHERE ship_id=? AND gate_id=? AND completed_tick=?");
                    j.text(1,std::to_string(a.actor));j.text(2,std::to_string(a.target));
                    j.i64(3,static_cast<std::int64_t>(a.tick));
                    if(!j.row())throw std::runtime_error("history: missing jump completion source");
                    const auto id=parse_u64(sqlite3_column_text(j.get(),0));
                    Statement start(db_,"SELECT generation FROM historical_events WHERE command_id=?");
                    start.text(1,std::to_string(id));
                    Statement boundary(db_,"SELECT start_generation FROM physical_meta WHERE singleton=1");
                    if(!boundary.row())throw std::runtime_error("history: missing physical boundary");
                    const auto physical_start=sqlite3_column_int64(boundary.get(),0);
                    if(start.row() && sqlite3_column_int64(start.get(),0)>physical_start){
                        add_physical(PhysicalIncident{"journey:end:"+std::to_string(id),next,a.tick,id,
                            PhysicalKind::JourneyCompleted,std::to_string(sqlite3_column_int64(j.get(),3)),
                            std::to_string(sqlite3_column_int64(j.get(),4)),sqlite3_column_int64(j.get(),5),
                            sqlite_str(j.get(),1)+":"+sqlite_str(j.get(),2)});
                    }
                } else if(a.kind==ActivityKind::ShipDestroyed || a.kind==ActivityKind::NpcLost){
                    add_physical(PhysicalIncident{(a.kind==ActivityKind::ShipDestroyed?"wreck:":"death:")+
                            std::to_string(a.start_command_id)+
                            (a.kind==ActivityKind::NpcLost?":"+std::to_string(a.target):""),
                        next,a.tick,a.start_command_id,
                        a.kind==ActivityKind::ShipDestroyed?PhysicalKind::ShipDestroyed:PhysicalKind::NpcLost,
                        std::to_string(a.actor),std::to_string(a.target),1,
                        a.kind==ActivityKind::ShipDestroyed?"wreck_persists":"individual_identity_lost"});
                }
            }
            if (fail == FailPoint::AfterReceiptWrite)
                throw std::runtime_error("injected failure after receipt changes");
            Statement snapshot(db_, "INSERT INTO snapshots(generation,format,state,checksum,ore_total,credit_total) VALUES(?,?,?,?,?,?)");
            snapshot.i64(1, static_cast<std::int64_t>(next));
            snapshot.i64(2, 1); snapshot.blob(3, bytes);
            snapshot.text(4, std::to_string(state.state_hash()));
            snapshot.i64(5, origin); snapshot.i64(6, credit_origin); snapshot.execute();
            Statement op(db_, "INSERT INTO operation_log(generation,kind,command,state_hash,tick) VALUES(?,?,?,?,?)");
            op.i64(1, static_cast<std::int64_t>(next));
            op.i64(2, expected_generation == 0 ? 0 : accepted ? 1 : 2);
            if (accepted) op.text(3, command_record(*accepted));
            op.text(4, std::to_string(state.state_hash()));
            op.i64(5, static_cast<std::int64_t>(state.tick()));
            op.execute();
            // Keep three last immutable checkpoints for offline inspection; never silently recover
            // from a corrupt latest snapshot because its effects/receipts may be newer.
            Statement prune(db_, "DELETE FROM snapshots WHERE generation < ?");
            prune.i64(1, static_cast<std::int64_t>(next > 2 ? next - 2 : 1));
            prune.execute();
            if (fail == FailPoint::AfterSnapshotWrite)
                throw std::runtime_error("injected failure after snapshot write");
            run(db_, "COMMIT");
        } catch (...) {
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
            throw;
        }
    }
    std::vector<HistoricalEvent> recent_history(std::size_t limit) const {
        if(limit==0 || limit>256) throw std::invalid_argument("history limit must be 1..256");
        Statement q(db_, "SELECT event_id,command_id,generation,tick,actor_id,target_id,action,"
                         "outcome,transferred FROM historical_events ORDER BY generation DESC, event_id DESC LIMIT ?");
        q.i64(1,static_cast<std::int64_t>(limit));
        std::vector<HistoricalEvent> entries;
        while(q.row()) {
            HistoricalEvent e;
            const auto str=[&](int col)->std::string {
                const auto* p=sqlite3_column_text(q.get(),col);
                if(!p)throw std::runtime_error("invalid history row");
                return reinterpret_cast<const char*>(p);
            };
            e.event_id=str(0);e.command_id=parse_u64(sqlite3_column_text(q.get(),1));
            e.generation=static_cast<std::uint64_t>(sqlite3_column_int64(q.get(),2));
            e.tick=static_cast<std::uint64_t>(sqlite3_column_int64(q.get(),3));
            e.actor=parse_u64(sqlite3_column_text(q.get(),4));
            e.target=parse_u64(sqlite3_column_text(q.get(),5));
            e.action=static_cast<Action>(sqlite3_column_int(q.get(),6));
            e.result=static_cast<Outcome>(sqlite3_column_int(q.get(),7));
            e.transferred=sqlite3_column_int64(q.get(),8);
            entries.push_back(e);
        }
        return entries;
    }
    std::vector<PhysicalIncident> recent_physical_incidents(std::size_t limit) const {
        if(limit==0 || limit>256)throw std::invalid_argument("physical history limit must be 1..256");
        Statement q(db_,"SELECT incident_id,generation,tick,command_id,kind,source,destination,amount,detail "
                        "FROM physical_incidents ORDER BY generation DESC,incident_id DESC LIMIT ?");
        q.i64(1,static_cast<std::int64_t>(limit));
        std::vector<PhysicalIncident> out;
        while(q.row()){
            PhysicalIncident e;
            e.incident_id=sqlite_str(q.get(),0);
            e.generation=static_cast<std::uint64_t>(sqlite3_column_int64(q.get(),1));
            e.tick=static_cast<std::uint64_t>(sqlite3_column_int64(q.get(),2));
            e.command_id=parse_u64(sqlite3_column_text(q.get(),3));
            e.kind=static_cast<PhysicalKind>(sqlite3_column_int(q.get(),4));
            e.source=sqlite_str(q.get(),5);e.destination=sqlite_str(q.get(),6);
            e.amount=sqlite3_column_int64(q.get(),7);e.detail=sqlite_str(q.get(),8);
            out.push_back(std::move(e));
        }
        return out;
    }
    // Explicit repair on a COPY ONLY. This never operates on the original file.
    void repair_latest_from_journal() {
        quick_integrity(db_);
        const auto newest = latest_generation(db_);
        if (newest < 2) throw std::runtime_error("recovery: no earlier checkpoint available");
        Statement cp(db_, "SELECT generation,state,checksum,ore_total,credit_total FROM snapshots "
                          "WHERE generation<? ORDER BY generation DESC");
        cp.i64(1, static_cast<std::int64_t>(newest));
        std::string reconstructed;
        std::int64_t ore = -1;
        bool found = false;
        while (cp.row()) {
            const auto gen = static_cast<std::uint64_t>(sqlite3_column_int64(cp.get(), 0));
            const auto size = sqlite3_column_bytes(cp.get(), 1);
            const auto* ptr = static_cast<const char*>(sqlite3_column_blob(cp.get(), 1));
            if (gen == 0 || size <= 0 || size > 16 * 1024 * 1024 || !ptr) continue;
            try {
                Simulation sim = Simulation::from_snapshot(std::string(ptr, static_cast<std::size_t>(size)));
                const auto hash = parse_u64(sqlite3_column_text(cp.get(), 2));
                ore = sqlite3_column_int64(cp.get(), 3);
                const auto credit_total=sqlite3_column_int64(cp.get(),4);
                if (sim.state_hash() != hash || sim.total_ore() != ore || sim.total_credits()!=sqlite3_column_int64(cp.get(),4)) continue;
                Statement journal(db_, "SELECT generation,kind,command,state_hash,tick FROM operation_log "
                                       "WHERE generation>? AND generation<=? ORDER BY generation");
                journal.i64(1, static_cast<std::int64_t>(gen));
                journal.i64(2, static_cast<std::int64_t>(newest));
                auto previous = gen;
                while (journal.row()) {
                    const auto g = static_cast<std::uint64_t>(sqlite3_column_int64(journal.get(), 0));
                    const auto kind = sqlite3_column_int(journal.get(), 1);
                    if (g != previous + 1 || g > newest) throw std::runtime_error("recovery: missing operation");
                    if (kind == 1) {
                        const auto* raw = sqlite3_column_text(journal.get(), 2);
                        if (!raw || !sim.submit(decode_command(reinterpret_cast<const char*>(raw))))
                            throw std::runtime_error("recovery: command replay refused");
                    } else if (kind == 2) {
                        sim.step();
                    } else throw std::runtime_error("recovery: invalid journal kind");
                    if (sim.state_hash() != parse_u64(sqlite3_column_text(journal.get(), 3)) ||
                        sim.tick() != static_cast<std::uint64_t>(sqlite3_column_int64(journal.get(), 4)) ||
                        sim.total_ore() != ore || sim.total_credits()!=credit_total)
                        throw std::runtime_error("recovery: replay hash or resource mismatch");
                    previous = g;
                }
                if (previous != newest) continue;
                Statement expected(db_, "SELECT checksum,ore_total,credit_total FROM snapshots WHERE generation=?");
                expected.i64(1, static_cast<std::int64_t>(newest));
                if (!expected.row() || sim.state_hash() != parse_u64(sqlite3_column_text(expected.get(),0)) ||
                    (ore != sqlite3_column_int64(expected.get(),1) || credit_total != sqlite3_column_int64(expected.get(),2))) continue;
                reconstructed = sim.canonical_state();
                found = true;
                break;
            } catch (const std::exception&) {
                // Try another retained checkpoint. Never guess around missing operations.
            }
        }
        if (!found) throw std::runtime_error("recovery: cannot verify latest state; refusing repair");
        run(db_, "BEGIN IMMEDIATE");
        try {
            if (latest_generation(db_) != newest) throw std::runtime_error("recovery: source changed");
            Statement fix(db_, "UPDATE snapshots SET state=? WHERE generation=?");
            fix.blob(1, reconstructed);
            fix.i64(2, static_cast<std::int64_t>(newest));
            fix.execute();
            if (sqlite3_changes(db_) != 1) throw std::runtime_error("recovery: snapshot not found");
            run(db_, "COMMIT");
        } catch (...) { sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr); throw; }
        Simulation checked(1);
        std::uint64_t gen = 0;
        std::int64_t origin = 0, credit_origin=0;
        if (!restore(checked, gen, origin, credit_origin) || gen != newest)
            throw std::runtime_error("recovery: repaired copy did not pass full validation");
    }
private:
    sqlite3* db_ = nullptr;
};

void PersistentWorld::recover_copy(const std::string& source, const std::string& destination) {
    if (source.empty() || destination.empty() || source == destination)
        throw std::invalid_argument("recovery requires distinct nonempty paths");
    // Reserve destination with an OS-level exclusive create: an existing file,
    // including a dangling symlink, cannot be clobbered by a path race.
#ifdef _WIN32
    int reserved = -1;
    const errno_t open_error = _sopen_s(&reserved, destination.c_str(),
        _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY, _SH_DENYNO, _S_IREAD | _S_IWRITE);
    if (open_error != 0) throw std::runtime_error("recovery: destination already exists or cannot be created");
    _close(reserved);
#else
    const int reserved = open(destination.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
    if (reserved < 0) throw std::runtime_error("recovery: destination already exists or cannot be created");
    close(reserved);
#endif
    sqlite3* src = nullptr;
    sqlite3* dest = nullptr;
    try {
        if (sqlite3_open_v2(source.c_str(), &src, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
            throw std::runtime_error("recovery: cannot open original read-only");
        if (sqlite3_open_v2(destination.c_str(), &dest, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK)
            throw std::runtime_error("recovery: cannot create destination");
        sqlite3_backup* backup = sqlite3_backup_init(dest, "main", src, "main");
        if (!backup) throw std::runtime_error("recovery: SQLite backup initialization failed");
        const int rc = sqlite3_backup_step(backup, -1);
        const int finish = sqlite3_backup_finish(backup);
        if (rc != SQLITE_DONE || finish != SQLITE_OK)
            throw std::runtime_error("recovery: SQLite backup failed");
        sqlite3_close(dest); dest = nullptr;
        sqlite3_close(src); src = nullptr;
        Store copy(destination);
        copy.repair_latest_from_journal();
    } catch (...) {
        if (dest) sqlite3_close(dest);
        if (src) sqlite3_close(src);
        // Delete only the attempted destination; never remove/modify the original.
        std::error_code ec;
        std::filesystem::remove(destination, ec);
        std::filesystem::remove(destination + "-wal", ec);
        std::filesystem::remove(destination + "-shm", ec);
        throw;
    }
}

PersistentWorld::PersistentWorld(const std::string& path, Simulation initial_world)
    : store_(std::make_unique<Store>(path)), world_(std::move(initial_world)) {
    if (!store_->restore(world_, generation_, resource_origin_, credit_origin_)) {
        if (world_.tick() != 0 || !world_.events().empty() || world_.accepted_command_count() != 0)
            throw std::invalid_argument("initial world must be at tick zero without commands");
        resource_origin_ = world_.total_ore();
        credit_origin_ = world_.total_credits();
        store_->commit(world_, 0, resource_origin_, credit_origin_, nullptr, {}, {}, {}, FailPoint::None);
        generation_ = 1;
    }
}
PersistentWorld::~PersistentWorld() = default;

bool PersistentWorld::submit(const Command& command, FailPoint fail) {
    if (command.action == Action::HostDestroyShip || command.action == Action::HostCrewLoss ||
        command.action == Action::SetGatePairOnline ||
        command.action == Action::SetGateStaff ||
        command.action == Action::DeliverGateSpares ||
        command.issuer == Simulation::kServerAuthority) return false;
    auto next = world_;
    if (!next.submit(command)) return false;
    store_->commit(next, generation_, resource_origin_, credit_origin_, &command, {}, {}, {}, fail);
    world_.swap(next); // noexcept: in-memory state published only after COMMIT
    ++generation_;
    return true;
}

bool PersistentWorld::submit_testing_gate_change(EntityId forward,EntityId reverse,
                                                  bool online,std::uint64_t command_id,
                                                  FailPoint fail) {
    const auto* a=world_.find(forward),*b=world_.find(reverse);
    if (!a || !b || a->type!=EntityType::Stargate || b->type!=EntityType::Stargate ||
        forward==reverse || a->system_id!=b->gate_destination ||
        b->system_id!=a->gate_destination) return false;
    Command c{};
    c.id=command_id;c.sequence=command_id;c.execute_tick=world_.tick()+1;
    c.issuer=Simulation::kServerAuthority;c.actor=reverse;c.target=forward;
    c.action=Action::SetGatePairOnline;c.amount=online?1:0;
    auto next=world_;
    if (!next.submit(c)) return false;
    store_->commit(next,generation_,resource_origin_,credit_origin_,&c,{},{},{},fail);
    world_.swap(next);
    ++generation_;
    return true;
}

bool PersistentWorld::submit_testing_gate_operations(EntityId forward,EntityId reverse,
        Action action,std::int64_t amount,std::uint64_t command_id,FailPoint fail) {
    if(action!=Action::SetGateStaff && action!=Action::DeliverGateSpares)return false;
    const auto* a=world_.find(forward),*b=world_.find(reverse);
    if(!a || !b || forward==reverse || a->type!=EntityType::Stargate ||
       b->type!=EntityType::Stargate || a->system_id!=b->gate_destination ||
       b->system_id!=a->gate_destination || !world_.gate_operations(forward,reverse))return false;
    Command c{};
    c.id=command_id;c.sequence=command_id;c.execute_tick=world_.tick()+1;
    c.issuer=Simulation::kServerAuthority;c.actor=reverse;c.target=forward;
    c.action=action;c.amount=amount;
    auto next=world_;
    if(!next.submit(c))return false;
    store_->commit(next,generation_,resource_origin_,credit_origin_,&c,{},{},{},fail);
    world_.swap(next);++generation_;
    return true;
}
bool PersistentWorld::submit_testing_destroy_ship(EntityId attacker, EntityId victim,
        std::uint64_t command_id,FailPoint fail) {
    Command c{};c.id=command_id;c.sequence=command_id;c.execute_tick=world_.tick()+1;
    c.issuer=Simulation::kServerAuthority;c.actor=attacker;c.target=victim;
    c.action=Action::HostDestroyShip;
    auto next=world_;
    if(!next.submit(c))return false;
    store_->commit(next,generation_,resource_origin_,credit_origin_,&c,{},{},{},fail);
    world_.swap(next);++generation_;return true;
}
bool PersistentWorld::submit_testing_crew_loss(EntityId ship,std::int64_t count,
        std::uint64_t command_id,FailPoint fail) {
    Command c{};c.id=command_id;c.sequence=command_id;c.execute_tick=world_.tick()+1;
    c.issuer=Simulation::kServerAuthority;c.actor=ship;c.target=ship;
    c.action=Action::HostCrewLoss;c.amount=count;
    auto next=world_;
    if(!next.submit(c))return false;
    store_->commit(next,generation_,resource_origin_,credit_origin_,&c,{},{},{},fail);
    world_.swap(next);++generation_;return true;
}
void PersistentWorld::step(FailPoint fail) {
    auto next = world_;
    const auto before = next.events().size();
    const auto before_activity = next.activity_events().size();
    std::map<std::uint64_t, Command> due;
    // One logical step processes precisely the commands scheduled for tick+1.
    if (auto it = next.commands_.find(next.tick() + 1); it != next.commands_.end()) {
        for (const auto& c : it->second) due.emplace(c.id, c);
    }
    next.step();
    const std::vector<Event> new_events(next.events().begin() + static_cast<std::ptrdiff_t>(before), next.events().end());
    const std::vector<ActivityEvent> new_activity(
        next.activity_events().begin() + static_cast<std::ptrdiff_t>(before_activity), next.activity_events().end());
    store_->commit(next, generation_, resource_origin_, credit_origin_, nullptr, new_events, due, new_activity, fail);
    world_.swap(next);
    ++generation_;
}

std::vector<HistoricalEvent> PersistentWorld::recent_history(std::size_t limit) const {
    return store_->recent_history(limit);
}
std::vector<PhysicalIncident> PersistentWorld::recent_physical_incidents(std::size_t limit) const {
    return store_->recent_physical_incidents(limit);
}
void PersistentWorld::steps(std::uint64_t n) { for (std::uint64_t i = 0; i < n; ++i) step(); }
} // namespace galaxy
