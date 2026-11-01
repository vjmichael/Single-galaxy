#pragma once
// ECON-03c6a: camera/player-distance *interest*, not simulation time or
// ownership. The host must durably hand off authority before acknowledging
// any cold/detailed simulation-mode transition.
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace galaxy::regional_events {
struct PositionMeters {double x=0, y=0, z=0;};
struct RegionBounds {std::string id; PositionMeters center; double radius_m=0;};
enum class RegionMode {Cold, Detailed};
struct InterestPolicy {
    double activate_m=2000;    // example tuning values; not gameplay constants
    double deactivate_m=3000;  // larger to provide hysteresis
    std::uint64_t minimum_dwell_ticks=0;
};
struct InterestDecision {
    RegionMode current=RegionMode::Cold;
    RegionMode desired=RegionMode::Cold;
    double nearest_surface_m=0;
    bool transition_requested=false;
};
class RegionalInterestManager {
public:
    explicit RegionalInterestManager(InterestPolicy policy);
    // Call when a camera/player changes interest, not once per NPC/tick.
    // Multiple observers: one nearby player keeps a region detailed.
    InterestDecision consider(const RegionBounds& region,
                              const std::vector<PositionMeters>& observers,
                              std::uint64_t current_tick);
    // Acknowledgment MUST happen only after host durable promotion/demotion.
    void acknowledge(const std::string& region_id,RegionMode mode,
                     std::uint64_t current_tick);
    RegionMode mode(const std::string& region_id) const;
private:
    struct Tracking {RegionMode mode=RegionMode::Cold;
                     std::uint64_t changed_tick=0;bool initialized=false;};
    InterestPolicy policy_;
    std::map<std::string,Tracking> regions_;
};
} // namespace galaxy::regional_events
