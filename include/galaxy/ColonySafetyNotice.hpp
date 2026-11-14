#pragma once
// COLONY-AUTO-04D.3: local opportunity + durable notification outbox.
// The outbox records proximity, not a proven NPC belief, opinion or social relay.
// Only a trusted world host may acknowledge actual delivery.
#include "galaxy/Simulation.hpp"
#include <cstdint>
#include <vector>
struct sqlite3;
namespace galaxy::colony_safety::notice {
struct Pending {
    std::uint64_t event_id=0,npc_id=0,world_generation=0,tick=0;
    EntityId site=0;
    int risk=0;
};
void publish(sqlite3* db,const Simulation& state,std::uint64_t generation);
void verify(sqlite3* db,const Simulation& state,std::uint64_t generation);
std::vector<Pending> pending(sqlite3* db,std::uint64_t limit=100);
// Host-only: confirms an actually delivered notification; not a judgment of
// awareness or intent. Never exposed to browser/player action ingress.
bool acknowledge(sqlite3* db,const Simulation& current,std::uint64_t generation,
                 std::uint64_t event_id,std::uint64_t npc_id);
std::uint64_t opportunity_count(sqlite3* db);
std::uint64_t acknowledgement_count(sqlite3* db);
}
