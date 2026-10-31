#include "galaxy/Simulation.hpp"

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace galaxy;

#define CHECK(expr) do { if (!(expr)) throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + " CHECK failed: " #expr); } while (false)

Command thrust(std::uint64_t id, std::uint64_t tick, std::uint64_t seq, EntityId actor, int x, int y, PlayerId issuer = 7) {
    Command c;
    c.id = id;
    c.execute_tick = tick;
    c.sequence = seq;
    c.actor = actor;
    c.issuer = issuer;
    c.action = Action::Thrust;
    c.thrust_x = x;
    c.thrust_y = y;
    return c;
}

Command mine(std::uint64_t id, std::uint64_t tick, std::uint64_t seq, EntityId actor, EntityId target, std::int64_t quantity, PlayerId issuer = 7) {
    Command c;
    c.id = id;
    c.execute_tick = tick;
    c.sequence = seq;
    c.actor = actor;
    c.issuer = issuer;
    c.target = target;
    c.action = Action::Mine;
    c.amount = quantity;
    return c;
}

std::string replay(std::uint64_t seed) {
    Simulation world(seed);
    world.add_station({0, 0});
    const auto ship = world.add_ship(7, {0, 0}, 100);
    const auto asteroid = world.add_asteroid({1000, 0}, 200);
    world.generate_asteroids(5);
    CHECK(world.submit(thrust(1, 1, 1, ship, 1, 0)));
    CHECK(world.submit(mine(2, 2, 1, ship, asteroid, 15)));
    CHECK(world.submit(thrust(3, 3, 1, ship, 0, -1)));
    CHECK(world.submit(mine(4, 4, 1, ship, asteroid, 25)));
    world.steps(10);
    return world.canonical_state();
}

void deterministic_replay() {
    CHECK(replay(12345) == replay(12345));
    CHECK(replay(12345) != replay(12346));
}

void reproducible_field() {
    Simulation a(50), b(50), c(51);
    a.generate_asteroids(20);
    b.generate_asteroids(20);
    c.generate_asteroids(20);
    CHECK(a.canonical_state() == b.canonical_state());
    CHECK(a.canonical_state() != c.canonical_state());
    CHECK(a.entities().size() == 20);
}

void inertia_and_fixed_step() {
    Simulation w(1);
    const auto ship = w.add_ship(7, {0, 0}, 10);
    CHECK(w.submit(thrust(1, 1, 1, ship, 1, 0)));
    w.step();
    CHECK(w.find(ship)->velocity.x == 250);
    CHECK(w.find(ship)->position.x == 250);
    w.steps(3);
    CHECK(w.find(ship)->position.x == 1000);
    CHECK(w.find(ship)->velocity.x == 250);
}

void authorization_and_invalid_actor() {
    Simulation w(1);
    const auto ship = w.add_ship(7, {0, 0}, 10);
    CHECK(w.submit(thrust(1, 1, 1, ship, 1, 0, 100)));
    CHECK(w.submit(thrust(2, 1, 2, 987654, 1, 0)));
    CHECK(w.submit(thrust(3, 1, 3, ship, 2, 0)));
    w.step();
    CHECK(w.events().size() == 3);
    CHECK(w.events()[0].result == Outcome::Unauthorized);
    CHECK(w.events()[1].result == Outcome::InvalidActor);
    CHECK(w.events()[2].result == Outcome::InvalidArgument);
    CHECK(w.find(ship)->position.x == 0);
}

void transfers_conserve_resources() {
    Simulation w(1);
    const auto ship = w.add_ship(7, {0, 0}, 30);
    const auto asteroid = w.add_asteroid({3000, 4000}, 80);
    CHECK(w.submit(mine(1, 1, 1, ship, asteroid, 70)));
    CHECK(w.submit(mine(2, 1, 2, ship, asteroid, 7)));
    w.step();
    CHECK(w.events()[0].result == Outcome::Applied);
    CHECK(w.events()[0].transferred == 30);
    CHECK(w.events()[1].result == Outcome::CargoFull);
    CHECK(w.find(ship)->cargo == 30);
    CHECK(w.find(asteroid)->ore == 50);
    CHECK(w.find(ship)->cargo + w.find(asteroid)->ore == 80);
}

void distance_and_invalid_mining() {
    Simulation w(1);
    const auto ship = w.add_ship(7, {0, 0}, 30);
    const auto far_rock = w.add_asteroid({5001, 0}, 10);
    const auto near_rock = w.add_asteroid({0, 0}, 10);
    CHECK(w.submit(mine(1, 1, 1, ship, far_rock, 5)));
    CHECK(w.submit(mine(2, 1, 2, ship, near_rock, 0)));
    CHECK(w.submit(mine(3, 1, 3, ship, 999999, 2)));
    w.step();
    CHECK(w.events()[0].result == Outcome::OutOfRange);
    CHECK(w.events()[1].result == Outcome::InvalidArgument);
    CHECK(w.events()[2].result == Outcome::InvalidTarget);
    CHECK(w.find(ship)->cargo == 0);
    CHECK(w.find(far_rock)->ore == 10);
    CHECK(w.find(near_rock)->ore == 10);
}

void duplicate_and_past_commands_rejected() {
    Simulation w(1);
    const auto ship = w.add_ship(7, {0, 0}, 10);
    const auto c = thrust(15, 1, 1, ship, 1, 1);
    CHECK(w.submit(c));
    CHECK(!w.submit(c));
    w.step();
    CHECK(!w.submit(thrust(16, 1, 1, ship, 1, 1)));
    CHECK(w.events().size() == 1);
}

void stable_order_with_reversed_submission() {
    Simulation a(1), b(1);
    const auto sa = a.add_ship(7, {0, 0}, 5);
    const auto sb = b.add_ship(7, {0, 0}, 5);
    const auto aa = a.add_asteroid({1000, 0}, 30);
    const auto ab = b.add_asteroid({1000, 0}, 30);
    auto c1 = mine(1, 1, 20, sa, aa, 5);
    auto c2 = mine(2, 1, 10, sa, aa, 3);
    CHECK(a.submit(c1)); CHECK(a.submit(c2));
    c1.actor = sb; c1.target = ab;
    c2.actor = sb; c2.target = ab;
    CHECK(b.submit(c2)); CHECK(b.submit(c1));
    a.step(); b.step();
    CHECK(a.events()[0].command_id == 2);
    CHECK(b.events()[0].command_id == 2);
    CHECK(a.events()[0].transferred == 3);
    CHECK(a.events()[1].transferred == 2);
    CHECK(a.canonical_state() == b.canonical_state());
}

void bounded_movement() {
    Simulation w(1);
    const auto s = w.add_ship(7, {Simulation::kWorldBoundMm - 10, 0}, 10);
    CHECK(w.submit(thrust(1, 1, 1, s, 1, 0)));
    w.step();
    CHECK(w.find(s)->position.x == Simulation::kWorldBoundMm);
    CHECK(w.find(s)->velocity.x == 0);
    CHECK(w.submit(thrust(2, 2, 1, s, -1, 0)));
    w.step();
    CHECK(w.find(s)->position.x == Simulation::kWorldBoundMm - 250);
}

void empty_ore_and_bad_spawns() {
    Simulation w(1);
    const auto s = w.add_ship(7, {0, 0}, 30);
    const auto rock = w.add_asteroid({0, 0}, 1);
    CHECK(w.submit(mine(1, 1, 1, s, rock, 1)));
    CHECK(w.submit(mine(2, 1, 2, s, rock, 1)));
    w.step();
    CHECK(w.events()[1].result == Outcome::NoResources);
    bool caught = false;
    try { w.add_station({Simulation::kWorldBoundMm + 1, 0}); }
    catch (const std::invalid_argument&) { caught = true; }
    CHECK(caught);
}

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"deterministic_replay", deterministic_replay},
        {"reproducible_field", reproducible_field},
        {"inertia_and_fixed_step", inertia_and_fixed_step},
        {"authorization_and_invalid_actor", authorization_and_invalid_actor},
        {"transfers_conserve_resources", transfers_conserve_resources},
        {"distance_and_invalid_mining", distance_and_invalid_mining},
        {"duplicate_and_past_commands_rejected", duplicate_and_past_commands_rejected},
        {"stable_order_with_reversed_submission", stable_order_with_reversed_submission},
        {"bounded_movement", bounded_movement},
        {"empty_ore_and_bad_spawns", empty_ore_and_bad_spawns},
    };
    int passed = 0;
    for (const auto& test : tests) {
        try { test.second(); std::cout << "PASS " << test.first << '\n'; ++passed; }
        catch (const std::exception& ex) { std::cerr << "FAIL " << test.first << ": " << ex.what() << '\n'; }
    }
    std::cout << passed << "/" << tests.size() << " tests passed\n";
    return passed == static_cast<int>(tests.size()) ? 0 : 1;
}
