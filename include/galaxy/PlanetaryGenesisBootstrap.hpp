#pragma once
// Stage6-C: ONE original-world, explicitly financed finite planetary
// source for *new* game genesis only. Never invoke on a loaded save.
// All bodies receive zero-mass immutable envelopes; one physically
// compatible source receives 38 original tonnes under the SAME world ledger.
#include "galaxy/Simulation.hpp"
#include <cstdint>

namespace galaxy::stage6 {
inline bool provision_first_planet_seed(Simulation& seed) {
    seed.ensure_physical_celestial_systems();
    seed.ensure_planetary_genesis_manifest();
    for(const auto& [body,genesis]:seed.planetary_genesis()) {
        if(!genesis.envelope.water_coverage)continue;
        const auto map=planetary::generate_watersheds(
            genesis.key,genesis.envelope);
        for(int index=0;index<planetary::kWatershedCells;++index) {
            if(map.cells[index].ocean)continue;
            // This is SOURCE genesis, not a player gift: no factory,
            // harvest, ship supply, terrain item or water entitlement.
            seed.provision_planetary_body_genesis(
                body,{6000,12000,16000,4000});
            seed.place_planetary_region_genesis(
                body,
                static_cast<std::uint16_t>(index/planetary::kWatershedColumns),
                static_cast<std::uint16_t>(index%planetary::kWatershedColumns),
                {6000,12000,16000,4000});
            seed.enable_planetary_source_custody();
            return true;
        }
    }
    // All-water or zero-water systems cannot be used as a fictitious
    // source for this explicitly dry-land introductory resource patch.
    seed.enable_planetary_source_custody(); // no physical source created
    return false;
}
} // namespace galaxy::stage6
