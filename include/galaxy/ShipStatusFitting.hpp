#pragma once
// Read the EXISTING fitted ship entity. This is not a new module registry.
// Installed hardpoint IDs 1 scanner, 2 probe launcher and 3 intrusion
// emitter are exposed; the passive firewall is in the support rack.
// The trusted host supplies a verified compromise surface and operator intent.
#include "galaxy/Simulation.hpp"
#include "galaxy/ShipStatusEffects.hpp"
#include <array>
#include <vector>

namespace galaxy::ship_status {
inline std::vector<ModuleControl> fitted_controls(
    const Entity& ship,
    const std::array<bool,2>& pilot_intent,
    const std::array<bool,2>& host_verified_attack_surface,
    const std::array<bool,2>& isolated) {
    std::vector<ModuleControl> modules;
    if(ship.type!=EntityType::Ship)return modules;
    for(std::uint8_t slot=0;slot<2;++slot) {
        const auto hardware=ship.fitting.hardpoints[slot];
        if(hardware!=1 && hardware!=2 && hardware!=3 && hardware!=4)continue;
        modules.push_back({ModuleKey{0,slot,hardware},
            pilot_intent[slot],host_verified_attack_surface[slot],
            isolated[slot]});
    }
    // The intrinsic safety sensor, physical/manual propulsion and empty
    // slots are never handed to the randomized module-control engine.
    return modules;
}
// This is a read-only capability projection. The original Simulation
// command executor must enforce it when native attack admissions are wired.
inline bool effective_hardpoint_on(
    const Engine& effects,const Entity& ship,std::uint64_t tick,
    std::uint8_t slot,const std::array<bool,2>& pilot_intent,
    const std::array<bool,2>& host_verified_attack_surface,
    const std::array<bool,2>& isolated) {
    if(slot>=2 || ship.fitting.hardpoints[slot]==0)return false;
    for(const auto& state:effects.project(ship.id,tick,
        fitted_controls(ship,pilot_intent,host_verified_attack_surface,isolated)))
        if(state.key.rack==0 && state.key.slot==slot)
            return state.effective_on;
    return false;
}
} // namespace galaxy::ship_status
