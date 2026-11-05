# PLAY-13D PGF-01B — Continuous Terrain and Watershed Generation

**Code status:** first C++17 native coarse-grid implementation committed. **Exact-head Windows/Linux verification pending.** Still a candidate geography layer, not authority for collectable water or an interactive map renderer.

## Implementation

`include/galaxy/PlanetaryWatershed.hpp` generates a deterministic, versioned `WatershedMap` for each real `PlanetaryWorldKey` / `PlanetaryEnvelope`, interoperating with the existing `Astronomy.hpp` hash and PGF-01A world-identity / envelope validation.

- **Elevation:** 36×72 fixed-grid, multi-scale, integer bilinear-interpolated coherent noise. Longitude wraps and neighbor routing crosses the seam. Geographic resolution remains deliberately coarse; no fabricated planet-local source identities.
- **Ocean mask:** sorted elevation-percentile coast threshold based on existing global `water_coverage`. Zero-water planets retain dry inland drainage rather than an automatically created sea. This is a procedural surface classification, not debit/credit of the physical genesis water budget.
- **Depression resolution:** deterministic priority-flood seeded by ocean cells (or lowest terrestrial basin when there is no ocean); recorded filled spill height, exact downstream parent and drainage roots. Downstream links are acyclic because parents are assigned to previously popped cells. Flow moves toward lower or equal filled spill potential. The algorithm currently permits internal depression fill for *routing only*, not the creation of liquid lakes or free water.
- **Catchment accumulation:** each cell provides an annual abstract rainfall input, which is routed along its unique downstream parent to terminal outlet. Sum of terminal discharges must equal sum of candidate rainfall across the full map. Overflows are explicitly rejected.
- **Rainfall approximation:** latitude/elevation/exposure/rain-shadow heuristic for initial test coverage. Arid means generally lower rainfall but allows wetter places; pressure, wind circulation, conservation of atmospheric moisture and real seasonal precipitation are future PGF-01C.
- **Channels:** accumulation and local input distinguish dry land, arroyo, seasonal stream and perennial river *potential*. A `PerennialRiver` label is **not** a world-backed perpetual liquid source. Never authorize water collection on generated score alone.

## Native acceptance

`tests/PLAY13D_PlanetaryWatershedTests.cpp`: complete-grid deterministic regeneration, exactly stable drainage parent, spill downhill property, no cycles or unanchored flow paths, conservation of aggregated precipitation at outlets, ocean percent and zero-ocean inland basin, cold/arid/temperate/airless envelope coverage, and revision-dependent variability. CMake `galaxy_play13d_planetary_watershed_tests`, workflow `play13d.yml` native Windows/Linux.

## Limits and mandatory follow-on work

1. Coherent elevation is not tectonic geology or mesh-grade terrain. Coast connectivity, low/high desert altitude calibration, orographic circulation, longitude seam derivatives, poles, and erosion remain schematic.
2. Priority flood is a correct bounded drainage-tree **topology**, but is not yet physically calibrated flow rate, connected-lake evaporation, water seasonality, groundwater, or aquifer flow. Multi-outlet depression handling can be improved with formal catchment IDs, spill edges and persistent lake basins.
3. Surface water cover is enforced as a coarse global fraction, not explicitly allocated from existing physical `OasisReserve` / world-genesis stock.
4. Existing `PlanetaryLandscape.hpp` has older independently seeded example arroyos/rivers. The next integration must replace that ad hoc watercourse placement with this watershed graph, rather than allow conflicting maps. Similarly `PlanetaryRegions.hpp` currently computes its own relief separately; PGF-01B terrain is not yet the universal source of terrain truth.
5. Frozen planets: water channels need thaw budget, river seasonal masks, ice cover, sea openings and brine checks. A candidate river on a −55°C surface is NOT guaranteed to flow.
6. Canonical persistence, validated `PersistentWorld` commits, regional survey discovery, local mining/farming and independent water mass custody have not been connected to the watershed prototype.
7. Tests above verify properties of the mathematical model. Real player end-to-end arrival → local survey → excavation/building/extraction → save/restart is a separate acceptance gate.

**Recommended next step: PGF-01B.2 single-source terrain/hydrology integration** — reconcile older regional elevation and ad hoc rivers with the new map, introduce explicit watershed IDs and coherent catchment wetness, then move on to physically constrained seasonal discharge PGF-01C.
