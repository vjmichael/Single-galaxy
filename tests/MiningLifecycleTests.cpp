#include "galaxy/Persistence.hpp"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace galaxy;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + " failed: " #x); } while(false)

struct Db {
    fs::path dir, path;
    Db() {
        static unsigned id = 0;
        dir = fs::temp_directory_path() / ("galaxy_mining_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" + std::to_string(++id));
        fs::create_directories(dir);
        path = dir / "world.sqlite";
    }
    ~Db() { std::error_code error; fs::remove_all(dir, error); }
};
Command cmd(std::uint64_t id, std::uint64_t tick, EntityId actor, EntityId target, Action act, std::int64_t amount, PlayerId owner = 7) {
    Command c{};
    c.id = id; c.execute_tick = tick; c.actor = actor; c.target = target;
    c.action = act; c.amount = amount; c.issuer = owner;
    return c;
}
void must_throw(const std::function<void()>& fn) {
    bool threw = false;
    try { fn(); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
}
void direct_sql(const fs::path& path, const char* query) {
    sqlite3* db = nullptr;
    if (sqlite3_open(path.string().c_str(), &db) != SQLITE_OK) throw std::runtime_error("SQLite open failed");
    char* error = nullptr;
    const auto rc = sqlite3_exec(db, query, nullptr, nullptr, &error);
    std::string message = error ? error : "SQLite query failed";
    sqlite3_free(error);
    sqlite3_close(db);
    if (rc != SQLITE_OK) throw std::runtime_error(message);
}

void ten_asteroids_despawn_and_reopen() {
    Db db;
    Simulation seed(111);
    const auto ship = seed.add_ship(7, {0,0}, 100);
    std::vector<EntityId> rocks;
    for (int i = 0; i < 10; ++i) rocks.push_back(seed.add_asteroid({100 + 200*i, 0}, 10));
    {
        PersistentWorld world(db.path.string(), seed);
        for (int i = 0; i < 10; ++i) {
            CHECK(world.submit(cmd(100+i, i+1, ship, rocks[i], Action::Mine, 10)));
            world.step();
            CHECK(world.world().events().back().result == Outcome::Applied);
            CHECK(world.world().events().back().transferred == 10);
            CHECK(world.world().find(rocks[i]) == nullptr);
            CHECK(world.world().was_depleted(rocks[i]));
            CHECK(world.world().find(ship)->cargo == (i+1)*10);
            CHECK(world.world().total_ore() == 100);
        }
        int vanished = 0;
        for (const auto& a : world.world().activity_events())
            if (a.kind == ActivityKind::AsteroidDespawned) ++vanished;
        CHECK(vanished == 10);
        std::cout << "SCENARIO 1: 10/10 asteroids despawned; ship cargo=100/100; ore conserved=100\n";
    }
    PersistentWorld restored(db.path.string(), Simulation(11));
    CHECK(restored.world().entities().size() == 1);
    CHECK(restored.world().find(ship)->cargo == 100);
    for (auto r : rocks) CHECK(!restored.world().find(r) && restored.world().was_depleted(r));
    CHECK(restored.world().total_ore() == 100);
}
void automatic_hold_full_and_reopen() {
    Db db;
    Simulation seed(222);
    const auto ship = seed.add_ship(7, {0,0}, 10);
    const auto rock = seed.add_asteroid({500,0}, 20);
    {
        PersistentWorld world(db.path.string(), seed);
        CHECK(world.submit(cmd(200,1,ship,rock,Action::StartMining,4)));
        world.step(); // 4/10
        CHECK(world.world().find(ship)->cargo == 4);
        CHECK(world.world().active_mining().size() == 1);
        world.step(); // 8/10
        CHECK(world.world().find(ship)->cargo == 8);
        world.step(); // remaining capacity 2, then HALT
        CHECK(world.world().find(ship)->cargo == 10);
        CHECK(world.world().find(rock)->ore == 10);
        CHECK(world.world().active_mining().empty());
        CHECK(world.world().activity_events().back().kind == ActivityKind::MiningStopped);
        CHECK(world.world().activity_events().back().result == Outcome::CargoFull);
        const auto events = world.world().activity_events().size();
        world.steps(4); // no more mining (no new input)
        CHECK(world.world().activity_events().size() == events);
        CHECK(world.world().find(ship)->cargo == 10);
        CHECK(world.world().find(rock)->ore == 10);
        CHECK(world.world().total_ore() == 20);
        std::cout << "SCENARIO 2: automatic mining 4+4+2, HOLD_FULL at tick 3; cargo=10/10; asteroid=10/20\n";
    }
    PersistentWorld restored(db.path.string(), Simulation(0x1234));
    CHECK(restored.world().find(ship)->cargo == 10);
    CHECK(restored.world().find(rock)->ore == 10);
    CHECK(restored.world().active_mining().empty());
    CHECK(restored.world().activity_events().back().result == Outcome::CargoFull);
    CHECK(restored.world().total_ore() == 20);
}
void single_shot_full_reports_immediately() {
    Simulation s(1);
    auto ship=s.add_ship(7,{0,0},10), rock=s.add_asteroid({100,0},20);
    CHECK(s.submit(cmd(1,1,ship,rock,Action::Mine,20)));
    s.step();
    CHECK(s.events().back().result == Outcome::Applied && s.events().back().transferred == 10);
    CHECK(s.activity_events().back().result == Outcome::CargoFull);
    CHECK(s.find(ship)->cargo == 10 && s.find(rock)->ore == 10);
    CHECK(s.submit(cmd(2,2,ship,rock,Action::Mine,20)));
    s.step();
    CHECK(s.events().back().result == Outcome::CargoFull && s.events().back().transferred == 0);
}
void auto_depletion_and_restart() {
    Db db;
    Simulation seed(1);
    auto ship=seed.add_ship(7,{0,0},100), rock=seed.add_asteroid({100,0},10);
    {
        PersistentWorld w(db.path.string(), seed);
        CHECK(w.submit(cmd(10,1,ship,rock,Action::StartMining,4)));
        w.steps(3);
        CHECK(w.world().find(ship)->cargo == 10);
        CHECK(!w.world().find(rock));
        CHECK(w.world().was_depleted(rock));
        CHECK(w.world().active_mining().empty());
        CHECK(w.world().activity_events().back().result == Outcome::NoResources);
        CHECK(w.world().total_ore() == 10);
        w.step();
    }
    PersistentWorld reopened(db.path.string(), Simulation(88));
    CHECK(reopened.world().total_ore() == 10);
    CHECK(reopened.world().find(ship)->cargo == 10);
    CHECK(reopened.world().was_depleted(rock));
}
void auto_pending_survives_restart() {
    Db db;
    Simulation seed(2);
    auto ship=seed.add_ship(7,{0,0},12), rock=seed.add_asteroid({100,0},80);
    {
        PersistentWorld w(db.path.string(), seed);
        CHECK(w.submit(cmd(10,1,ship,rock,Action::StartMining,3)));
        w.step();
        CHECK(w.world().find(ship)->cargo == 3);
    }
    {
        PersistentWorld r(db.path.string(), Simulation(90));
        CHECK(r.world().active_mining().count(ship) == 1);
        r.steps(3);
        CHECK(r.world().find(ship)->cargo == 12);
        CHECK(r.world().active_mining().empty());
    }
    PersistentWorld r(db.path.string(), Simulation(91));
    CHECK(r.world().find(rock)->ore == 68 && r.world().total_ore() == 80);
    CHECK(!r.submit(cmd(10,5,ship,rock,Action::StartMining,3)));
}
void stop_command_and_no_resume() {
    Simulation w(3);
    auto ship=w.add_ship(7,{0,0},100), rock=w.add_asteroid({100,0},80);
    CHECK(w.submit(cmd(1,1,ship,rock,Action::StartMining,10)));
    w.step();
    CHECK(w.find(ship)->cargo == 10);
    CHECK(w.submit(cmd(2,2,ship,rock,Action::StopMining,0)));
    w.step();
    CHECK(w.find(ship)->cargo == 10);
    CHECK(w.active_mining().empty());
    w.steps(5);
    CHECK(w.find(ship)->cargo == 10);
    CHECK(w.activity_events().back().result == Outcome::Applied);
}
void start_checks_authorization_and_range() {
    Simulation w(4);
    auto ship=w.add_ship(7,{0,0},10), rock=w.add_asteroid({6000,0},20);
    CHECK(w.submit(cmd(1,1,ship,rock,Action::StartMining,3,999)));
    CHECK(w.submit(cmd(2,1,ship,rock,Action::StartMining,3)));
    w.step();
    CHECK(w.events()[0].result == Outcome::Unauthorized);
    CHECK(w.events()[1].result == Outcome::OutOfRange);
    CHECK(w.active_mining().empty());
    CHECK(w.total_ore() == 20);
}
void failpoint_auto_rolls_back_and_retries() {
    Db db;
    Simulation seed(5);
    auto ship=seed.add_ship(7,{0,0},10),rock=seed.add_asteroid({100,0},20);
    PersistentWorld w(db.path.string(), seed);
    CHECK(w.submit(cmd(1,1,ship,rock,Action::StartMining,6)));
    auto before=w.world().canonical_state();
    must_throw([&] { w.step(FailPoint::AfterReceiptWrite); });
    CHECK(w.world().canonical_state() == before);
    must_throw([&] { w.step(FailPoint::AfterSnapshotWrite); });
    CHECK(w.world().canonical_state() == before);
    PersistentWorld fresh(db.path.string(), Simulation(0));
    CHECK(fresh.world().canonical_state() == before);
    w.step();
    CHECK(w.world().find(ship)->cargo == 6);
    CHECK(w.world().find(rock)->ore == 14);
    CHECK(w.world().total_ore() == 20);
}
void recover_auto_cycles_on_copy() {
    Db db;
    auto dest=db.dir/"recovered.sqlite";
    Simulation seed(6);
    auto ship=seed.add_ship(7,{0,0},10),rock=seed.add_asteroid({100,0},20);
    std::string expected;
    {
        PersistentWorld w(db.path.string(), seed);
        CHECK(w.submit(cmd(1,1,ship,rock,Action::StartMining,4)));
        w.steps(3);
        expected=w.world().canonical_state();
    }
    direct_sql(db.path, "UPDATE snapshots SET state=x'424144' WHERE generation=(SELECT MAX(generation) FROM snapshots)");
    must_throw([&] { PersistentWorld invalid(db.path.string(), Simulation(1)); });
    PersistentWorld::recover_copy(db.path.string(), dest.string());
    PersistentWorld recovered(dest.string(), Simulation(1));
    CHECK(recovered.world().canonical_state() == expected);
    CHECK(recovered.world().total_ore() == 20);
    CHECK(recovered.world().find(ship)->cargo == 10);
    CHECK(recovered.world().find(rock)->ore == 10);
    must_throw([&] { PersistentWorld invalid(db.path.string(), Simulation(1)); });
}
void tampered_auto_ledger_refused() {
    Db db;
    Simulation seed(7);
    auto ship=seed.add_ship(7,{0,0},10),rock=seed.add_asteroid({100,0},20);
    {
        PersistentWorld w(db.path.string(), seed);
        CHECK(w.submit(cmd(1,1,ship,rock,Action::StartMining,4)));
        w.step();
    }
    direct_sql(db.path, "UPDATE resource_ledger SET amount=99 WHERE reason='automated_mining_transfer'");
    must_throw([&] { PersistentWorld w(db.path.string(), Simulation(0)); });
}
void concurrent_deterministic_depletion() {
    Simulation seed(8);
    auto a=seed.add_ship(7,{0,0},100), b=seed.add_ship(8,{0,0},100);
    auto rock=seed.add_asteroid({100,0},10);
    CHECK(seed.submit(cmd(1,1,a,rock,Action::StartMining,7,7)));
    CHECK(seed.submit(cmd(2,1,b,rock,Action::StartMining,7,8)));
    seed.step();
    CHECK(seed.find(a)->cargo == 7);
    CHECK(seed.find(b)->cargo == 3);
    CHECK(seed.find(rock) == nullptr);
    CHECK(seed.active_mining().empty());
    CHECK(seed.total_ore() == 10);
}
void automatic_stop_on_range_exit() {
    Simulation w(11);
    auto ship=w.add_ship(7,{4800,0},100), rock=w.add_asteroid({0,0},100);
    CHECK(w.submit(cmd(1,1,ship,rock,Action::StartMining,5)));
    w.step();
    CHECK(w.find(ship)->cargo == 5);
    Command thrust=cmd(2,2,ship,0,Action::Thrust,0);
    thrust.thrust_x=1;
    CHECK(w.submit(thrust));
    w.steps(2); // ship now 5550 mm away, no longer in range
    CHECK(w.find(ship)->cargo == 5);
    CHECK(w.active_mining().empty());
    CHECK(w.activity_events().back().result == Outcome::OutOfRange);
    CHECK(w.total_ore() == 100);
}
void legacy_zero_asteroid_is_cleaned_on_next_step() {
    // Historical v1 snapshots could contain empty asteroids. Preserve their
    // exact bytes until a new tick migrates the in-world lifecycle state.
    const std::string old =
        "galaxy-kernel-v1|1|0|3|17\n"
        "entity|1|1|0|7|0|0|0|0|0|0|10\n"
        "entity|2|1|1|0|100|0|0|0|0|0|0\n";
    auto s=Simulation::from_snapshot(old);
    CHECK(s.canonical_state() == old);
    CHECK(s.find(2) != nullptr);
    Db db;
    {
        PersistentWorld world(db.path.string(), s);
        world.step();
        CHECK(!world.world().find(2));
        CHECK(world.world().was_depleted(2));
        CHECK(world.world().total_ore() == 0);
    }
    PersistentWorld reopened(db.path.string(), Simulation(1));
    CHECK(reopened.world().was_depleted(2));
}
void direct_mining_cancels_other_automatic_miner() {
    Simulation w(12);
    auto s1=w.add_ship(7,{0,0},20), s2=w.add_ship(8,{0,0},20),rock=w.add_asteroid({100,0},10);
    CHECK(w.submit(cmd(1,1,s1,rock,Action::StartMining,4,7)));
    w.step();
    CHECK(w.find(s1)->cargo == 4);
    CHECK(w.submit(cmd(2,2,s2,rock,Action::Mine,6,8)));
    w.step();
    CHECK(!w.find(rock));
    CHECK(w.active_mining().empty());
    CHECK(w.find(s1)->cargo == 4 && w.find(s2)->cargo == 6);
    CHECK(w.total_ore() == 10);
    CHECK(w.activity_events().back().kind == ActivityKind::MiningStopped);
    CHECK(w.activity_events().back().result == Outcome::NoResources);
}
void tampered_auto_ledger_target_refused() {
    Db db;
    Simulation seed(13);
    auto ship=seed.add_ship(7,{0,0},10),rock=seed.add_asteroid({100,0},20);
    {
        PersistentWorld w(db.path.string(), seed);
        CHECK(w.submit(cmd(1,1,ship,rock,Action::StartMining,4)));
        w.step();
    }
    direct_sql(db.path,"UPDATE resource_ledger SET source_id='999' WHERE reason='automated_mining_transfer'");
    must_throw([&]{PersistentWorld w(db.path.string(),Simulation(0));});
}
int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests={
        {"ten_asteroids_despawn_and_reopen",ten_asteroids_despawn_and_reopen},
        {"automatic_hold_full_and_reopen",automatic_hold_full_and_reopen},
        {"single_shot_full_reports_immediately",single_shot_full_reports_immediately},
        {"auto_depletion_and_restart",auto_depletion_and_restart},
        {"auto_pending_survives_restart",auto_pending_survives_restart},
        {"stop_command_and_no_resume",stop_command_and_no_resume},
        {"start_checks_authorization_and_range",start_checks_authorization_and_range},
        {"failpoint_auto_rolls_back_and_retries",failpoint_auto_rolls_back_and_retries},
        {"recover_auto_cycles_on_copy",recover_auto_cycles_on_copy},
        {"tampered_auto_ledger_refused",tampered_auto_ledger_refused},
        {"concurrent_deterministic_depletion",concurrent_deterministic_depletion},
        {"automatic_stop_on_range_exit",automatic_stop_on_range_exit},
        {"legacy_zero_asteroid_is_cleaned_on_next_step",legacy_zero_asteroid_is_cleaned_on_next_step},
        {"direct_mining_cancels_other_automatic_miner",direct_mining_cancels_other_automatic_miner},
        {"tampered_auto_ledger_target_refused",tampered_auto_ledger_target_refused}
    };
    int passed=0;
    for(const auto& [name,test]:tests) {
        try{test(); std::cout<<"PASS "<<name<<"\n"; ++passed;}
        catch(const std::exception& e){std::cerr<<"FAIL "<<name<<": "<<e.what()<<"\n";}
    }
    std::cout << passed << "/" << tests.size() << " mining lifecycle checks passed\n";
    return passed == static_cast<int>(tests.size()) ? 0 : 1;
}
