# S6-E — Planetary Ecology and Industry Integration (v130)

Date: 2026-11-11. Development branch: sg/stage6d-physical-environment-20261111.
State: SOURCE STAGED. Native/installed acceptance OPEN. No merge/release.

## Original-world integration

S6-E reuses existing Simulation, PersistentWorld original SQLite writer,
PlanetarySiteFacility, WO3 named NPCs, original utility, farm inventories,
WO4 consumer shortages and WO5 real shuttle custody. There is NO second
planet, matter source, weather-based inventory, fictional NPC/worker or
automatic shipment.

### E1 — Actual soil and moisture ecology

- PlanetaryEcology.hpp uses S6-D original region physical phases, finite
  stable topsoil, actual surface or aquifer liquid, original biosphere and
  last committed hazard. Forecast precipitation cannot create water.
- Open-field crops require original viable soil >=100 kg, surface/aquifer
  liquid >=100 kg, an original biosphere, and clear hazards.
- The exact existing installed farm uses a powered controlled mode when
  open-field conditions are missing. Exposed flood blocks crops.

### E2 — Conserved production and physically extracted soil

- Open-field batch: 10kg water + 5kg nutrients -> 12kg food + 3kg
  wastewater. This is the original matter-conserving crop recipe.
- Ordinary indoor batch: same 15kg input -> 11kg food + 4kg wastewater,
  costing 2 original electricity units.
- Cold/heat/drought indoor batch: 10kg food + 5kg wastewater,
  costing 3 units. Material is never lost or silently created.
- At least 100kg of genuine S6-D5 extracted soil held in the original
  site inventory can serve as reusable indoor soil medium. It increases
  recovered food by 1kg and decreases residual wastewater by 1kg while
  reducing energy cost by one unit. Soil stays at that physical site;
  it is not an invented reusable supply in a different location.

### E3 — Real machinery, utilities and worker custody

- Flooding halts exposed site construction/production and blocks solar
  power opportunity. Real deployed people still consume original
  provisions. Existing project and workforce stall counters advance.
- Cold/heat raise the original utility HVAC draw from 1 to 3 energy units
  per tick. Solar service buffer and existing loss-bearing water/oxygen
  recycling remain unchanged otherwise.
- Manual farming under v130 requires two physically deployed original
  available NPCs and actual working farm machinery, provisions, existing
  water and nutrients. Autonomous work continues to require actual
  original crew presence, existing equipment and original policy.
- Original site now keeps ecological field-batch, controlled-batch,
  blocked-work, last-tick and hazard evidence counters. These are not
  new crops, workers or material balances.

### E4 — WO4 and WO5

- WO4ResourceDemand.hpp provides an owner-scoped, read-only ecological
  production signal for actual planetary surface sites: crop viability,
  original finite soil, physical hazard, utility power and present
  original worker readiness. No prospective harvest is added to stock,
  and original consumer-based reserve deficits remain unchanged.
- WO5PhysicalResupply.hpp remains responsible for physically approved
  shuttle loads, descent, local unloads and committed recoverable
  transfers. No site material is teleported into a ship.
- S6-D5 onward freight for soil/minerals/volatiles remains OPEN.

### E5 — Native visibility and durable migration

- Existing owner-only PLANET_SITE_STATUS now emits PLANET_ECOLOGY:
  actual body/cell, field/controlled/blocked cycles, power, hazard,
  physical soil, surface liquid, aquifer. Viewer rejects invalid source,
  impossible hazard, negative/out-of-bounds/unsupported fields.
- Canonical galaxy-kernel-v130 emits one planetecology record per original
  built site, including zero counters. Snapshot parsing rejects
  missing, duplicate or orphaned site rows, future tick and hazard, and
  ecological harvest claims exceeding original farm batch counts.
- Zero-resource migration from historical v129 via
  Simulation::enable_planetary_ecology and
  PersistentWorld::ensure_planetary_ecology uses the original SQLite
  generation transaction and unchanged original matter totals.
  Native colony and desktop startup and new-world genesis enable v130.

## Source-staged regression evidence

- Stage06EcologyIndustryTests.cpp / galaxy_stage06_ecology_industry:
  original fixed mass, weather hazards, drought, greenhouse power,
  depleted topsoil, extracted site-soil efficiency, zero-funding
  limitations, v129->v130 rollback, idempotent SQLite reopening,
  forged ecological row refusal, unchanged world original mass.
- PLAY13D_PlanetSurveyNodeTests.js checks PLANET_ECOLOGY strict decoding.
- Stage06AuthorizedSourceTests.cpp expectations updated for new v130
  genesis; historical v129 restore is still supported.

No C++ compile, CTest, Node test execution, live host, installed Godot,
native worker positive crop matrix, physical Windows/Radeon, or
multi-cell load test was run. These are OPEN acceptance items.

## Remaining downstream work

1. S6-E extended ecological lifecycle: season-long crop timing, actual
   vegetation/biodiversity, soil nutrients/biota, contamination,
   reclamation and broader local industry input/output. This change
   adds actual ecological constraints to the existing farm but does
   NOT implement a complete autonomous planetary biosphere.
2. S6-D cross-region water/groundwater/sediment transport; positive
   actual original D5 extraction and soil/mineral/volatile freight.
3. S6-F genuine continuous navigable planet arrival/terrain.
4. S6-G exact-head native Windows/Linux acceptance, SQLite corruption
   and restart, installed Godot UI, original NPC/industry and
   physical Radeon owner-operated tests.

S6-E core integration source staged. Extended ecology, original freight,
S6-D outstanding transport and ALL native acceptance remain OPEN.
