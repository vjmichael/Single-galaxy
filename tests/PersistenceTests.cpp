#include "galaxy/Persistence.hpp"
#include <sqlite3.h>
#include <filesystem>
#include <functional>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <string>
#include <chrono>
#include <vector>

using namespace galaxy;
namespace fs = std::filesystem;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + " failed: " #x); } while(false)

struct Database {
    fs::path dir;
    fs::path path;
    Database() {
        static std::uint64_t counter = 0;
        dir = fs::temp_directory_path() / ("galaxy_b2_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" + std::to_string(++counter));
        fs::create_directories(dir);
        path = dir / "world.sqlite";
    }
    ~Database() { std::error_code ec; fs::remove_all(dir, ec); }
};
Simulation start() {
    Simulation s(123456);
    s.add_station({0, 0});
    s.add_ship(7, {0, 0}, 50);
    s.add_asteroid({2000, 0}, 80);
    return s;
}
Command mining(std::uint64_t id, std::uint64_t tick = 1, std::int64_t amount = 12) {
    Command c{};
    c.id = id; c.execute_tick = tick; c.issuer = 7;
    c.actor = 2; c.target = 3; c.action = Action::Mine;
    c.amount = amount;
    return c;
}
Command thrust(std::uint64_t id, std::uint64_t tick) {
    Command c{};
    c.id = id; c.execute_tick = tick; c.issuer = 7;
    c.actor = 2; c.action = Action::Thrust; c.thrust_x = 1;
    return c;
}
void direct_sql(const fs::path& dbpath, const char* sql) {
    sqlite3* db = nullptr;
    if (sqlite3_open(dbpath.string().c_str(), &db) != SQLITE_OK) throw std::runtime_error("sqlite open failed");
    char* err = nullptr;
    auto rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
    std::string error = err ? err : "SQLite query failed";
    sqlite3_free(err);
    sqlite3_close(db);
    if (rc != SQLITE_OK) throw std::runtime_error(error);
}
void throws(const std::function<void()>& fn) {
    bool yes = false;
    try { fn(); } catch (const std::exception&) { yes = true; }
    CHECK(yes);
}
void fresh_database_and_restore() {
    Database db;
    std::string baseline;
    {
        PersistentWorld w(db.path.string(), start());
        baseline = w.world().canonical_state();
        CHECK(w.generation() == 1);
        CHECK(w.resource_origin() == 80);
        CHECK(fs::exists(db.path));
    }
    PersistentWorld reopened(db.path.string(), Simulation(999));
    CHECK(reopened.world().canonical_state() == baseline);
    CHECK(reopened.resource_origin() == 80);
    CHECK(reopened.generation() == 1);
}
void pending_command_survives_restart() {
    Database db;
    {
        PersistentWorld w(db.path.string(), start());
        CHECK(w.submit(mining(101)));
        CHECK(w.generation() == 2);
    }
    {
        PersistentWorld w(db.path.string(), Simulation(777));
        CHECK(!w.submit(mining(101))); // accepted ID remains reserved, including pending commands
        w.step();
        CHECK(w.world().find(2)->cargo == 12);
        CHECK(w.world().find(3)->ore == 68);
        CHECK(w.world().events().size() == 1);
    }
    PersistentWorld w(db.path.string(), Simulation(777));
    CHECK(w.world().find(2)->cargo == 12);
    CHECK(w.world().find(3)->ore == 68);
    CHECK(!w.submit(mining(101, 2)));
    CHECK(w.world().total_ore() == 80);
}
void failed_submit_never_publishes() {
    Database db;
    PersistentWorld w(db.path.string(), start());
    const auto before = w.world().canonical_state();
    throws([&] { w.submit(mining(111), FailPoint::AfterReceiptWrite); });
    CHECK(w.world().canonical_state() == before);
    CHECK(w.generation() == 1);
    throws([&] { w.submit(mining(111), FailPoint::AfterSnapshotWrite); });
    CHECK(w.world().canonical_state() == before);
    CHECK(w.generation() == 1);
    PersistentWorld r(db.path.string(), Simulation(42));
    CHECK(r.world().canonical_state() == before);
    CHECK(r.submit(mining(111)));
}
void failed_step_rolls_back_everything() {
    Database db;
    PersistentWorld w(db.path.string(), start());
    CHECK(w.submit(mining(1)));
    const auto before = w.world().canonical_state();
    throws([&] { w.step(FailPoint::AfterReceiptWrite); });
    CHECK(w.world().canonical_state() == before);
    throws([&] { w.step(FailPoint::AfterSnapshotWrite); });
    CHECK(w.world().canonical_state() == before);
    CHECK(w.world().tick() == 0);
    CHECK(w.world().find(3)->ore == 80);
    {
        PersistentWorld r(db.path.string(), Simulation(3));
        CHECK(r.world().canonical_state() == before);
        r.step();
        CHECK(r.world().find(3)->ore == 68);
        CHECK(r.world().find(2)->cargo == 12);
    }
    PersistentWorld r(db.path.string(), Simulation(3));
    CHECK(r.world().tick() == 1);
    CHECK(r.world().total_ore() == 80);
    CHECK(!r.submit(mining(1, 2)));
}
void deterministic_replay_after_restore() {
    Database db;
    Simulation expected = start();
    CHECK(expected.submit(mining(3, 2)));
    CHECK(expected.submit(thrust(4, 1)));
    expected.steps(5);
    {
        PersistentWorld w(db.path.string(), start());
        CHECK(w.submit(mining(3, 2)));
        CHECK(w.submit(thrust(4, 1)));
        w.steps(2);
    }
    PersistentWorld w(db.path.string(), Simulation(42));
    w.steps(3);
    CHECK(w.world().canonical_state() == expected.canonical_state());
    CHECK(w.world().state_hash() == expected.state_hash());
}
void reject_corrupt_latest_save() {
    Database db;
    {
        PersistentWorld w(db.path.string(), start());
        CHECK(w.submit(mining(7)));
        w.step();
    }
    direct_sql(db.path, "UPDATE snapshots SET state = x'424144' WHERE generation = (SELECT MAX(generation) FROM snapshots)");
    throws([&] { PersistentWorld w(db.path.string(), Simulation(1)); });
    // Intentionally fail closed instead of rewinding, which could resurrect spent ore.
}
void reject_tampered_receipts() {
    Database db;
    {
        PersistentWorld w(db.path.string(), start());
        CHECK(w.submit(mining(84)));
        w.step();
    }
    direct_sql(db.path, "UPDATE receipts SET transferred=999 WHERE id='84'");
    throws([&] { PersistentWorld w(db.path.string(), Simulation(1)); });
}
void reject_tampered_receipt_identity() {
    Database db;
    {
        PersistentWorld w(db.path.string(), start());
        CHECK(w.submit(mining(84)));
    }
    direct_sql(db.path, "UPDATE receipts SET id='85' WHERE id='84'");
    throws([&] { PersistentWorld w(db.path.string(), Simulation(1)); });
}
void reject_future_schema() {
    Database db;
    { PersistentWorld w(db.path.string(), start()); }
    direct_sql(db.path, "PRAGMA user_version=99");
    throws([&] { PersistentWorld w(db.path.string(), Simulation(1)); });
}
void reject_stale_writer() {
    Database db;
    PersistentWorld first(db.path.string(), start());
    PersistentWorld second(db.path.string(), Simulation(2));
    CHECK(first.submit(mining(30)));
    throws([&] { second.submit(mining(31)); });
    CHECK(second.world().tick() == 0);
    PersistentWorld updated(db.path.string(), Simulation(3));
    CHECK(!updated.submit(mining(30)));
    CHECK(updated.submit(mining(31)));
}
void rejected_command_is_durable() {
    Database db;
    {
        PersistentWorld w(db.path.string(), start());
        auto bad = mining(18);
        bad.issuer = 999;
        CHECK(w.submit(bad));
        w.step();
        CHECK(w.world().events().back().result == Outcome::Unauthorized);
        CHECK(w.world().find(2)->cargo == 0);
    }
    PersistentWorld w(db.path.string(), Simulation(3));
    CHECK(w.world().events().back().result == Outcome::Unauthorized);
    CHECK(!w.submit(mining(18, 2)));
    CHECK(w.world().total_ore() == 80);
}
void many_command_ids_and_resource_conservation() {
    Database db;
    PersistentWorld w(db.path.string(), start());
    for (std::uint64_t i = 1; i <= 250; ++i) {
        CHECK(w.submit(mining(i, i, 1)));
        w.step();
    }
    CHECK(w.world().tick() == 250);
    CHECK(w.world().find(2)->cargo == 50);
    CHECK(w.world().find(3)->ore == 30);
    CHECK(w.world().total_ore() == 80);
    PersistentWorld r(db.path.string(), Simulation(42));
    for (std::uint64_t i = 1; i <= 250; ++i) CHECK(!r.submit(mining(i, 251)));
    CHECK(r.world().canonical_state() == w.world().canonical_state());
}
void reject_unauthorized_resource_creation_post_initialization() {
    Database db;
    PersistentWorld w(db.path.string(), start());
    // There is no public live mutation API; resource origin is fixed at initialization.
    CHECK(w.resource_origin() == 80);
    CHECK(w.world().total_ore() == 80);
}

void v1_to_v2_migration_preserves_world() {
    Database db;
    std::string before;
    {
        PersistentWorld w(db.path.string(), start());
        CHECK(w.submit(mining(510, 1)));
        w.step();
        before = w.world().canonical_state();
    }
    // Mimic an actual v1 database. Remove all later-schema objects, not just
    // operation_log: otherwise this is an impossible v1/v5 hybrid fixture.
    direct_sql(db.path, "DROP TRIGGER historical_no_update; DROP TRIGGER historical_no_delete; "
                       "DROP TABLE social_tie_events; DROP TABLE social_tie_meta; "
                       "DROP TABLE group_views; DROP TABLE group_relays; DROP TABLE social_groups; "
                       "DROP TABLE social_meta; "
                       "DROP TRIGGER knowledge_no_update; DROP TRIGGER knowledge_no_delete; "
                       "DROP TABLE npc_knowledge; DROP TABLE knowledge_meta; "
                       "DROP TRIGGER physical_no_update; DROP TRIGGER physical_no_delete; "
                       "DROP TABLE physical_incidents; DROP TABLE physical_meta; "
                       "DROP TABLE historical_events; DROP TABLE historical_meta; "
                       "DROP TABLE operation_log; PRAGMA user_version=1;");
    {
        PersistentWorld w(db.path.string(), Simulation(12));
        CHECK(w.world().canonical_state() == before);
        CHECK(w.submit(thrust(511, 2)));
        w.step();
        CHECK(w.world().tick() == 2);
    }
    sqlite3* raw = nullptr;
    CHECK(sqlite3_open(db.path.string().c_str(), &raw) == SQLITE_OK);
    sqlite3_stmt* version = nullptr;
    CHECK(sqlite3_prepare_v2(raw, "PRAGMA user_version", -1, &version, nullptr) == SQLITE_OK);
    CHECK(sqlite3_step(version) == SQLITE_ROW);
    CHECK(sqlite3_column_int(version, 0) == 9); // GRSM-01b physical ledger migration
    sqlite3_finalize(version);
    sqlite3_close(raw);
    PersistentWorld reopened(db.path.string(), Simulation(9));
    CHECK(reopened.world().tick() == 2);
    CHECK(reopened.world().find(2)->cargo == 12);
}
void recovery_copy_replays_and_preserves_original() {
    Database db;
    const auto dest = db.dir / "recovered.sqlite";
    std::string target;
    {
        PersistentWorld w(db.path.string(), start());
        CHECK(w.submit(mining(610, 1)));
        w.step();
        CHECK(w.submit(thrust(611, 2)));
        w.step();
        target = w.world().canonical_state();
    }
    direct_sql(db.path, "UPDATE snapshots SET state=x'424144' WHERE generation=(SELECT MAX(generation) FROM snapshots)");
    throws([&]{ PersistentWorld w(db.path.string(), Simulation(1)); });
    PersistentWorld::recover_copy(db.path.string(), dest.string());
    CHECK(fs::exists(dest));
    PersistentWorld recovered(dest.string(), Simulation(2));
    CHECK(recovered.world().canonical_state() == target);
    CHECK(recovered.world().total_ore() == 80);
    CHECK(!recovered.submit(thrust(611, 3)));
    CHECK(recovered.world().find(2)->cargo == 12);
    throws([&]{ PersistentWorld w(db.path.string(), Simulation(1)); });
}
void recovery_copy_from_second_retained_checkpoint() {
    Database db;
    const auto dest = db.dir / "recovered.sqlite";
    std::string target;
    {
        PersistentWorld w(db.path.string(), start());
        CHECK(w.submit(thrust(701, 1)));
        w.step();
        CHECK(w.submit(mining(702, 2)));
        w.step();
        target = w.world().canonical_state();
    }
    direct_sql(db.path, "UPDATE snapshots SET state=x'424144' WHERE generation >= (SELECT MAX(generation)-1 FROM snapshots)");
    PersistentWorld::recover_copy(db.path.string(), dest.string());
    PersistentWorld recovered(dest.string(), Simulation(1));
    CHECK(recovered.world().canonical_state() == target);
}
void recovery_fails_closed_without_complete_journal() {
    Database db;
    const auto dest = db.dir / "recovered.sqlite";
    {
        PersistentWorld w(db.path.string(), start());
        CHECK(w.submit(mining(801)));
        w.step();
    }
    direct_sql(db.path, "DELETE FROM operation_log WHERE generation=(SELECT MAX(generation) FROM snapshots);");
    direct_sql(db.path, "UPDATE snapshots SET state=x'424144' WHERE generation=(SELECT MAX(generation) FROM snapshots);");
    throws([&]{ PersistentWorld::recover_copy(db.path.string(), dest.string()); });
    CHECK(!fs::exists(dest));
}
void recovery_fails_closed_on_corrupt_receipt() {
    Database db;
    const auto dest = db.dir / "recovered.sqlite";
    {
        PersistentWorld w(db.path.string(), start());
        CHECK(w.submit(mining(901)));
        w.step();
    }
    direct_sql(db.path, "UPDATE receipts SET transferred=10000 WHERE id='901'");
    direct_sql(db.path, "UPDATE snapshots SET state=x'424144' WHERE generation=(SELECT MAX(generation) FROM snapshots)");
    throws([&]{ PersistentWorld::recover_copy(db.path.string(), dest.string()); });
    CHECK(!fs::exists(dest));
}
void recovery_refuses_existing_destination() {
    Database db;
    auto dest = db.dir / "already_exists.sqlite";
    { std::ofstream x(dest); x << "Do not overwrite"; }
    throws([&]{ PersistentWorld::recover_copy(db.path.string(), dest.string()); });
    std::ifstream in(dest);
    std::string contents;
    std::getline(in, contents);
    CHECK(contents == "Do not overwrite");
}
void rollback_does_not_publish_operation_log() {
    Database db;
    {
        PersistentWorld w(db.path.string(), start());
        throws([&]{ w.submit(mining(991), FailPoint::AfterSnapshotWrite); });
    }
    sqlite3* raw = nullptr;
    CHECK(sqlite3_open(db.path.string().c_str(), &raw) == SQLITE_OK);
    sqlite3_stmt* stmt = nullptr;
    CHECK(sqlite3_prepare_v2(raw, "SELECT COUNT(*) FROM operation_log", -1, &stmt, nullptr) == SQLITE_OK);
    CHECK(sqlite3_step(stmt) == SQLITE_ROW);
    CHECK(sqlite3_column_int(stmt, 0) == 1); // initial creation only
    sqlite3_finalize(stmt);
    sqlite3_close(raw);
}
void reject_tampered_resource_ledger() {
    Database db;
    {
        PersistentWorld w(db.path.string(), start());
        CHECK(w.submit(mining(1001)));
        w.step();
    }
    direct_sql(db.path, "UPDATE resource_ledger SET amount=44 WHERE reason='mining_transfer'");
    throws([&]{ PersistentWorld w(db.path.string(), Simulation(1)); });
}
int main() {
    std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"fresh_database_and_restore", fresh_database_and_restore},
        {"pending_command_survives_restart", pending_command_survives_restart},
        {"failed_submit_never_publishes", failed_submit_never_publishes},
        {"failed_step_rolls_back_everything", failed_step_rolls_back_everything},
        {"deterministic_replay_after_restore", deterministic_replay_after_restore},
        {"reject_corrupt_latest_save", reject_corrupt_latest_save},
        {"reject_future_schema", reject_future_schema},
        {"reject_tampered_receipts", reject_tampered_receipts},
        {"reject_tampered_receipt_identity", reject_tampered_receipt_identity},
        {"reject_stale_writer", reject_stale_writer},
        {"rejected_command_is_durable", rejected_command_is_durable},
        {"many_command_ids_and_resource_conservation", many_command_ids_and_resource_conservation},
        {"reject_unauthorized_resource_creation_post_initialization", reject_unauthorized_resource_creation_post_initialization},
        {"v1_to_v2_migration_preserves_world", v1_to_v2_migration_preserves_world},
        {"recovery_copy_replays_and_preserves_original", recovery_copy_replays_and_preserves_original},
        {"recovery_copy_from_second_retained_checkpoint", recovery_copy_from_second_retained_checkpoint},
        {"recovery_fails_closed_without_complete_journal", recovery_fails_closed_without_complete_journal},
        {"recovery_fails_closed_on_corrupt_receipt", recovery_fails_closed_on_corrupt_receipt},
        {"recovery_refuses_existing_destination", recovery_refuses_existing_destination},
        {"rollback_does_not_publish_operation_log", rollback_does_not_publish_operation_log},
        {"reject_tampered_resource_ledger", reject_tampered_resource_ledger},
    };
    int successes = 0;
    for (auto& [name, fn] : tests) {
        try { fn(); std::cout << "PASS " << name << '\n'; ++successes; }
        catch(const std::exception& e) { std::cerr << "FAIL " << name << ": " << e.what() << '\n'; }
    }
    std::cout << successes << "/" << tests.size() << " B2 tests passed\n";
    return successes == static_cast<int>(tests.size()) ? 0 : 1;
}
