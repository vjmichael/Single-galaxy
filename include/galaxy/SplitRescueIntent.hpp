#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <sqlite3.h>

namespace galaxy {
struct SplitRescueIntentPlan;
class Simulation;
void assert_split_execution_intent(sqlite3* db,const SplitRescueIntentPlan& plan,
                                   std::uint64_t current_generation);
void record_split_execution(sqlite3* db,const SplitRescueIntentPlan& plan,
                            std::uint64_t command_id,std::uint64_t generation,
                            std::uint64_t tick);
void record_split_crew_execution(sqlite3* db,const SplitRescueIntentPlan& plan,
                std::uint64_t command_id,std::uint64_t generation,
                const std::vector<std::uint64_t>& crew,std::size_t primary_count);
void verify_split_executions(const std::string& path,
                             std::uint64_t generation);
// Internal full-database verification. This receipt is a NON-EXECUTABLE rescue
// allocation intent, not evidence of evacuation or a new voyage.
void verify_split_rescue_intents(const std::string& sqlite_path,
                                 std::uint64_t latest_generation);
}
