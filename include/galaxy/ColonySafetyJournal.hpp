#pragma once
// COLONY-AUTO-04D.2. Exclusive canonical SQLite writer only.
// Records decisions, never performs production, repair, transport or evacuation.
#include "galaxy/Simulation.hpp"
#include <cstdint>
struct sqlite3;
namespace galaxy::colony_safety::journal {
void record_world_commit(sqlite3* db,const Simulation& next,
                         std::uint64_t previous_generation,
                         std::uint64_t new_generation);
// Verifies the journal anchor and physically recomputes every recorded site.
// A missing schema is valid only for pre-04D.2 worlds.
void verify(sqlite3* db,const Simulation& current,std::uint64_t generation);
std::uint64_t event_count(sqlite3* db);
}
