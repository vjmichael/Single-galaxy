#pragma once
// SPACE-05C: bounded, read-only OS worker process preflight.
// A subprocess receives ONLY a validated cell scheduling summary, never
// the original SQLite DB path or a mutable original-world handle.
// The selected writer is STILL fenced by PersistentWorld::Store::commit.
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace galaxy::spatial {
struct CellProcessTask {
    std::string scope;
    std::string worker;
    std::uint64_t original_generation=0;
    std::uint64_t original_hash=0;
    std::size_t physical_ships=0;
    std::uint8_t detail=0;
};
struct CellProcessOutcome {
    bool passed=false;
    std::string reason;
};
CellProcessOutcome run_cell_process(
    const std::string& executable,const CellProcessTask& task,
    std::chrono::milliseconds deadline=std::chrono::milliseconds(2000));
} // namespace galaxy::spatial
