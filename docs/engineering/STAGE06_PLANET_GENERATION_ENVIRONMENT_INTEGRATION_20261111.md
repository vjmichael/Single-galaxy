# Stage 6 — Planet Generation & Environmental Integration
## STAGE6-ENV01: Unified geography → seasonal climate → authenticated exploration

**Date:** 2026-11-11. **Branch:** `sg/group4-dock-colony-inventory-run-20261111`. **Status:** source staged; exact-head native/Windows/Linux and installed play acceptance OPEN. **DOCK-07/08/09:** continue parked provisionally. No PR merge/release authorized.

> **S6-C implementation checkpoint (2026-11-11):** `galaxy-kernel-v127` original world state now stores immutable body-specific planetary environments and finite explicit genesis-stock / unique regional allocations for water, soil, minerals, and volatiles. Zero-material migration for pre-v127 saves is inside the original `PersistentWorld` SQLite transaction; new-world native colony and installed desktop station creation share one bounded original planet-source seed (38t). Authenticated orbital planet scanning projects those genuine region stocks but grants NO extraction. Original `total_ore` includes each mass exactly once; staged `galaxy_stage06_finite_planet_genesis` and strict web decoder tests remain **UNEXECUTED**. [Full S6-C source and acceptance ledger](STAGE06C_IMMUTABLE_PLANET_GENESIS_RESOURCES_20261111.md).

### Existing upstream foundations reused

- Original `PhysicalCelestialBody`, system ID and immutable astronomical body ID from `Simulation`, not a new planet registry.
- `PlanetaryWorldKey`, `PlanetaryEnvelope`, the existing 36×72 `WatershedMap` and `survey_geographic_site` as the only source for relief, ocean mask and downstream drainage.
- Actual owner/fitted-powered-scanner admission in `Simulation::celestial_navigation_anchor`.
- Existing owner-private `SurveyArchive::record_planetary_region` with strict recorded geography immutability and original save/restart behavior.
- Same native `PLANET_SURVEY` command and `viewer/colony.js` UI rather than another app/writer.
- Actual constructed planetary sites, shuttles, farms, finite oasis reserves, NPC identities and transport continue governed by the original `Simulation` / `PersistentWorld`, NOT by forecast rainfall.

### Committed STAGE6-ENV01 additions

1. `include/galaxy/PlanetarySeasonalClimate.hpp`: `generate_seasonal_climate` projects **4 deterministic fixed-point quarters** over the already-approved canonical watershed. It computes temperature with latitude/relief/hemispheric phase, precipitation partition, cold snow storage, bounded warm snowmelt, evaporation, native downstream drainage routing, ocean sea-ice and bounded cold/heat/high-flow indicators. Map cells and their drainage parents are reused; forged cycles and mismatched model sources are refused.
2. **Every seasonal model unit is conserved:** annual projected precipitation equals projected evaporation + terminal runoff + final retained snowpack. Quarterly sums use integer cumulative fractions rather than platform-dependent floating point. No `kg` or actual water reserves are created. The original v1 map's potentially misleading airless-world rainfall *scores* are filtered out of the new seasonal projection **without rewriting previous owner-private v1 geography observations**; correcting historical map facts requires a separate generator revision/migration.
3. `Simulation::scan_planetary_seasons`: derives forecasts only for an existing original planet/moon, original owner and scanner-equipped physical ship, exact region key and original canonical map. It is pure, does not create a planet, water resource, plant, inventory or settlement, and requires no new SQLite writer.
4. `src/colony_live_host.cpp`: an already-authorized `PLANET_SURVEY` records source geography first and then emits four `PLANET_SEASON` records with the same source body, site coordinates and geography revision plus independent climate model revision. No new HTTP command or client-supplied season/source authority.
5. `viewer/colony.js`: strict numeric, revision, spatial and risk parser with source-matched four-quarter readback; concise temperature/ice/open-water/flow/risk UI and engineering caution. The display deliberately says **survey potential**, never physically extractable water or farming permission.
6. `tests/Stage06SeasonalEnvironmentTests.cpp` plus CMake target `galaxy_stage06_seasonal_environment`: 2592 cells x 4 quarters across temperate, arid, ocean, frozen, airless and zero-water environments. Asserts conservation, downstream terminal balance, seasonal variability, source mismatch refusals, no unsupported liquid on airless/frozen quarters, cycle/snow accounting and deterministic regeneration.
7. Existing `PLAY13D_LivePlanetSurveyTests.cpp`, `PLAY13D_LiveHostPlanetSurveySmoke.py`, and `PLAY13D_PlanetSurveyNodeTests.js`: source-staged tests for original scanner/owner denial, same-key original SQLite reopen, four native environmental frames, strict client parsing and no state/material mutation.

**No native C++ build, CTest, live host process, browser/Node runtime, CI, physical R9700 or installed app was run for this new source head.** Structural source inspection is not an execution pass. Source-level changes may require compiler/parser repairs in the consolidated integration.

### Stage 6 engineering work still required — priority order

| Group | Main implementation/integration | State |
| --- | --- | --- |
| **S6-A — Single planetary geography** | Keep only PGF-01B watershed authority for landforms and native survey; remove remaining active ad hoc prototype projection consumers; establish stable longitude-seam/polar and erosion/river-morphology behavior | **Partial existing implementation; acceptance open** |
| **S6-B — Seasonal environmental assessment** | Seasonal heat/precipitation/snow/ice/topological runoff joined to real body and native survey without granting resources | **STAGE6-ENV01 source staged, acceptance open** |
| **S6-C — Immutable physical planetary genesis** | v127 canonical original body envelope, genesis-only finite water/soil/mineral/volatile stocks, one-use geographically placed regional sources, original mass accounting, zero-material historical SQLite migration and source-backed scanner readback. See [S6-C report](STAGE06C_IMMUTABLE_PLANET_GENESIS_RESOURCES_20261111.md) | **SOURCE IMPLEMENTED provisionally; real native/installed acceptance OPEN** |
| **S6-D — Physical environmental processes** | Authenticated seasonal clock/weather hazards, ice/water phase transitions, aquifer/lake storage, soil erosion, meaningful heat/power/resource costs; durable original-transaction conservation and failure rollback | **Not implemented** |
| **S6-E — Ecology and industry integration** | Independent biosphere selection and finite ecology; water/nutrients/energy/seasonal suitability affect actual existing farmland, constructed planetary sites, named-worker work, supplies and downstream original WO4/WO5 logistics | **Not implemented for new PGF climate** |
| **S6-F — Actual planet arrival and playable terrain** | Map real body-relative coordinates to continuous surface navigation/landing, procedural terrain meshes LOD, original physical survey/installation colliders, terrain/no-fake-inventory telemetry, native player and NPC travel | **Not implemented in full** |
| **S6-G — Migration, scale, and acceptance** | Exact-head Windows/Linux CMake/CTest, source revision and survey archive migration, old-world conservation, 4-cell/long-run load, Godot/orbital UI, saved-world restart and physical Windows acceptance | **OPEN** |

### Environmental integration boundaries

- **A watershed is not a river inventory.** The v1 `WatershedCell.rainfall` and new `SeasonalCell.catchment_discharge` are model scores. Water extraction must continue using actual original finite `OasisReserve` / planet-source inventory and authorized `PersistentWorld` operations. No autogenerated lake may become a free material producer.
- **Projected habitat is not actual habitation.** A favorable quarterly temperature or modelled growing window cannot authorize a farm, shuttle landing, independent settlement, food production or child relocation. The original site, supply, worker and migration gates remain controlling.
- **Existing observations are immutable.** A future PGF algorithm or actual astronomical envelope upgrade needs a new revision and migration, not silent alteration of already recorded `SurveyArchive` v1 reports.
- **Single source of body identity.** Planet visuals, exploration and farming must not construct a second planet or invent mineral labels based solely on Godot or the browser.
- **Scale.** A 36×72 cached regional projection is a coarse world model, not final terrain mesh/physics. Fixed season processing is bounded; sustained multi-body rendering and cold/detailed operational scheduling remain separate tests.

### Next implementation step

**S6-D — Physical Environmental Processes.** S6-C's version-127 original body envelope and physical source allocation are now source-implemented provisionally, with no materials added to historical worlds. Next connect those finite original stocks to regional thermal/water/ice and soil behavior and ultimately authorized physical extraction, retaining original resource transfer/SQLite custody. S6-B/S6-C native and installed acceptance remain OPEN.

**Release/merge status: not authorized.**
