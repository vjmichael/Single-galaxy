#pragma once
// ECON-02B2d4a: saved blueprint -> ordered material-backed player-authorized jobs.
// Plan manifests are untrusted user documents. Only Simulation / PersistentWorld
// commands create equipment, spend modules, and record durable effects.
#include "galaxy/FactoryLayout.hpp"
#include "galaxy/Persistence.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace galaxy::industry {
struct BuildJob {
    Command command{}; // execute_tick is set only when a player authorizes execution
    std::string description;
    std::uint64_t blueprint_device_or_link=0;
    std::uint8_t module_cost=0;
};
struct ConstructionProgress {
    std::size_t completed=0, total=0;
    std::uint64_t modules_remaining=0;
    bool conflicting=false;
    bool cancelled=false;
    std::string reason;
    bool finished() const noexcept {return !conflicting && !cancelled && completed==total;}
};
class BlueprintConstruction {
public:
    // The blueprint's storage device ID 1 is outpost bin 0; additional storage
    // device ID n maps to bin n-1. UINT32_MAX denotes the existing Tier-1
    // workshop. Other device types are deferred until their world counterpart
    // can be created with real resource-backed commands.
    BlueprintConstruction(FactoryLayout blueprint,EntityId site,EntityId ship,
                          PlayerId owner,std::uint64_t plan_reference_id);
    static BlueprintConstruction from_manifest(const std::string& data);
    std::string manifest() const;
    // User-side cancellation stops future submissions; it never refunds or
    // dismantles already committed construction. Persist the returned manifest.
    void cancel() noexcept {cancelled_=true;}
    bool cancelled() const noexcept {return cancelled_;}
    const std::vector<BuildJob>& jobs() const noexcept {return jobs_;}
    ConstructionProgress inspect(const Simulation& world) const;
    // One explicit authorizing user call performs at most ONE host command.
    // Returns its committed event; refuses conflicting/replayed plans.
    Event execute_next(PersistentWorld& world,PlayerId authenticated_owner,
                       EntityId authenticated_ship,std::uint64_t fresh_command_id) const;
private:
    FactoryLayout blueprint_;
    EntityId site_,ship_;
    PlayerId owner_;
    std::uint64_t base_;
    bool cancelled_=false;
    std::vector<BuildJob> jobs_;
    void compile();
    bool job_done(const BuildJob& job,const Simulation& world) const;
    const OutpostStorageLink* find_link(const Simulation& world,std::uint64_t blueprint_id) const;
};
} // namespace galaxy::industry
