#pragma once
// S6-G: One original-world Stage 6 migration/admission rule shared by the
// installed station, cockpit, shuttle and planetary ground writers.
// No second SQLite file, generated resources, local Godot state, or
// background catch-up. Every mutation is the existing PersistentWorld
// generation-fenced durable Store::commit, with original OS writer custody.
//
// A read-only recovery probe deliberately does NOT call admit(): it must
// observe the exact currently committed source, then let the actual admitted
// writer apply any permitted version upgrade.
#include "galaxy/Persistence.hpp"
#include "galaxy/Stage6RecoveryDecision.hpp"
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace galaxy::stage6::consolidated {
struct Evidence {
    std::uint64_t tick=0,generation=0,state_hash=0;
    std::int64_t total_ore=0,total_material_kg=0,total_credits=0;
    std::size_t bodies=0,regional_sources=0,physical_regions=0;
    std::size_t sites=0,shuttles=0,people=0;
    bool atmosphere_v135=false,ecology_v130=false,continuous_v133=false;
};
inline Evidence observe(const PersistentWorld& source) {
    const auto& w=source.world();
    Evidence out;
    out.tick=w.tick();
    out.generation=source.generation();
    out.state_hash=w.state_hash();
    out.total_ore=w.total_ore();
    out.total_material_kg=w.total_material_kg();
    out.total_credits=w.total_credits();
    out.bodies=w.planetary_genesis().size();
    out.regional_sources=w.planetary_region_sources().size();
    out.physical_regions=w.planetary_physical_environment().size();
    out.sites=w.planetary_sites().size();
    out.shuttles=w.planetary_shuttles().size();
    out.people=w.player_people().size();
    out.atmosphere_v135=w.original_surface_atmosphere_enabled();
    out.ecology_v130=w.planetary_ecology_enabled();
    out.continuous_v133=w.continuous_native_planet_surface();
    if(!out.generation || out.total_ore!=source.resource_origin())
        throw std::logic_error("S6-G original resource/genesis journal mismatch");
    for(const auto& [key,shuttle]:w.planetary_shuttles()) {
        const auto s=w.planetary_sites().find(key);
        if(s==w.planetary_sites().end() ||
           !std::get<0>(key) || !shuttle.owner || !shuttle.source_ship ||
           (shuttle.flight_phase>0 && !shuttle.pilot_npc) ||
           shuttle.flight_phase>3 ||
           shuttle.owner!=s->second.owner ||
           shuttle.source_ship!=s->second.source_ship)
            throw std::logic_error("S6-G original site and named-shuttle identity diverged");
    }
    for(const auto& [id,p]:w.player_people()) {
        if(id!=p.owner)
            throw std::logic_error("S6-G original player identity diverged");
        if(p.state==PlayerPersonState::InPlanetaryShuttle ||
           p.state==PlayerPersonState::PlanetaryTransit ||
           p.state==PlayerPersonState::AtPlanetarySurface) {
            if(!stage6::recovery::inspect(w,id))
                throw std::logic_error("S6-G original physical player/shuttle source mismatch");
        }
    }
    // Reparse the CANONICAL authoritative state, not just a presentation
    // checksum. Original source is unchanged by this readback.
    const auto reconstructed=Simulation::from_snapshot(w.canonical_state());
    if(reconstructed.state_hash()!=out.state_hash ||
       reconstructed.total_ore()!=out.total_ore ||
       reconstructed.total_material_kg()!=out.total_material_kg ||
       reconstructed.total_credits()!=out.total_credits ||
       reconstructed.tick()!=out.tick)
        throw std::logic_error("S6-G canonical save cannot reconstruct identical original matter");
    return out;
}
inline Evidence admit(PersistentWorld& world) {
    const auto before=observe(world);
    // All these operations are ALREADY original PersistentWorld SQLite
    // transactions. Re-running on current head is idempotent and must
    // not alter a completed source or duplicate regional matter.
    world.ensure_planetary_genesis_manifest();         // v127
    world.ensure_physical_planetary_environment();     // v128
    world.ensure_planetary_source_custody();            // v129
    world.ensure_planetary_ecology();                   // v130
    world.ensure_planetary_atmosphere_manifest();       // v135 physical body
    const auto after=observe(world);
    if(!after.atmosphere_v135 || !after.ecology_v130 ||
       !after.continuous_v133 ||
       after.tick!=before.tick ||
       after.total_ore!=before.total_ore ||
       after.total_material_kg!=before.total_material_kg ||
       after.total_credits!=before.total_credits ||
       after.bodies<before.bodies ||
       after.regional_sources<before.regional_sources ||
       after.sites!=before.sites ||
       after.shuttles!=before.shuttles ||
       after.people!=before.people)
        throw std::logic_error("S6-G migration changed original ticks, matter, people or physical custody");
    return after;
}
} // namespace galaxy::stage6::consolidated
