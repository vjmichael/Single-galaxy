# PLAY-13D.3 — Planetary Generation Framework (PGF-01)
**Status:** authoritative design contract and preliminary deterministic adapter; physical climate/hydrology solver, planet construction, persistent storage and native acceptance remain separate gates.
**Branch:** `feature/play13d-planetary-agriculture-20261104` stacked on PLAY-13C. Canonical principle: **planetary class is a set of global constraints; environments are regional emergent outcomes.** A living planet is not a single biome, and a lifeless planet does not receive manufactured diversity to fill a quota.

## 1. Existing engine integration
- Entry authority: `generate_astronomical_profile(galaxy_seed,system_id)` from `Astronomy.hpp`; retain stars, orbital bands, stable body ID and galaxy seed, and `PhysicalCelestialBody` from `Simulation.hpp`. Do not fabricate new body IDs in the climate layer.
- Current regional prototype: `PlanetaryRegions.hpp` (36 x 72 region sampling), `PlanetaryLandscape.hpp` (arid landforms, dry/seasonal/perennial watercourse potentials) and `PlanetaryIceWorld.hpp` (thermal envelope, possible subglacial habitats). Those APIs currently represent *candidate surveys*, not implemented water physics or authoritative resource custody.
- PGF-01 adapter: `PlanetaryGenerationFramework.hpp` defines a domain-separated versioned world identity, type-independent global boundary conditions, and region sampling that calls the existing `survey_region` rather than replacing it. PGF generates no water, biomass, NPC, manufactured material or infrastructure. Region dimensions, generator revision and seeds must be persisted and migration-aware.

## 2. Generation pipeline and dependency ordering

| Stage | Generation inputs | Deterministic outputs | Critical constraint |
|---|---|---|---|
| 0. Astronomy | galaxy seed, system ID, astronomical profile, physical body ID, orbital zone | unique planetary identity, star/insolation inputs | Existing authoritative bodies, not recreated |
| 1. Global envelope | radius/gravity, atmosphere pressure/composition, axial tilt, rotation, mean insolation, thermal inertia, geothermal flux, bulk water/ice stock, age | broadly temperate/arid/ocean/frozen/airless classification, temperature/rainfall bounds | Classification never forces a biome |
| 2. Relief/geology | tectonic activity, lithology, impact history, erosion, seed | continuous elevation, shelves, mountains, valleys, sediment/soil substrates | Neighbor continuity, longitude seam, polar area correction |
| 3. Atmosphere/climate | insolation, latitude, season, rotation, currents, relief | pressure, winds, temperature, precipitation, evapotranspiration, snow and ice | Elevation lapse rates, rain shadows, heat transport; global envelope still bounds plausible extremes |
| 4. Water geography | finite water stocks, terrain, precipitation, snowmelt, aquifer permeability | ocean basins, coherent downhill rivers, lakes, arroyos, seasonal flow, groundwater, ice | Upstream drainage consistency and conserved water volumes |
| 5. Local microclimates | canyon depth/orientation, slope aspect, river distance, coast, cave light/groundwater, geothermal heat | shaded canyons, oases, wet riparian valleys, caves, brine pockets, meltwater | No guaranteed forest or liquid water from label alone |
| 6. Ecology | **independent world-genesis biosphere presence**, viable temperatures, water/chemistry, light or chemical energy, substrates | potential and actual vegetation/ecosystems, dormant seedbanks, seasonal blooms | Biosphere presence != habitability; known life needs accessible energy and matter |
| 7. Survey and play | region coordinates, sensor fidelity, prior observations | partial discovery and confidence, settlement/farm/resource risk, local site opportunities | Geological surveys reveal; exploration records persist |
| 8. Simulation | validated player actions, physical location and inventories, seasons, weather, settlement infrastructure | committed resource flows, cultivation, damage, survival, regeneration or depletion | `PersistentWorld` transactions, durable receipts, no free resource creation |

The engine may use coarse deterministic snapshots for distant worlds and expand only observed/inhabited regions. If expanded later, all generated sites must keep the same identity and geological facts; players must not experience reshuffled terrain after returning.

## 3. Rules for different worlds

**Living temperate planets:** Warm/cold latitude, coastal/interior and altitude variation; forests, grasslands, infertile rock, wetlands, rivers, deserts and frozen heights may coexist if climate supports them. Local farm productivity is not inferred from a global `LifeSupporting` label.

**Arid planets:** High exposed-land fraction is possible, not compulsory. High and low deserts, dry prairies, rain-shadow mountains, perennial rivers with finite upstream sources, summer/dry-season arroyos, wetter river valleys, canyon groves and skylit/groundwater-fed cave plants can coexist. A dry arroyo does not supply water until a precipitation or snowmelt event. Vegetation requires verified local water/energy/nutrient support and may become dormant.

**Frozen planets:** Example boundary: equatorial maximum 0°C (32°F), polar minimum −55°C (~−67°F); these are parameters, not universal ice-world constants. Seasonal meltwater, open-water polynyas or ocean openings require heat/energy budgets and compatible salinity/pressure, not just a coarse region label. Sea surfaces may remain icy while subglacial water persists. Geothermal/brine niches may support *possible* life; no Earth-like green surface forest merely because the world is labeled habitable.

**Ocean worlds:** Deep waters, shelves, lagoons, islands, sea ice and occasional exposed land can occur; seabed relief and shelf depth affect marine ecosystems and settlement. Ocean-dominated does not require 100% surface coverage.

**Sterile/airless worlds:** Allow near mono-environment global surface conditions while retaining geological and mineral diversity. No diversity floor or automatic oasis injection.

## 4. Data and version contract

- `PlanetaryWorldKey`: galaxy seed, system ID, original physical body ID, generator revision. No unstable `std::hash`, platform RNG, locale or current time.
- `PlanetaryEnvelope`: persisted global constraints and biosphere flag independently of class; input validation rejects contradictory climates, invalid water percentage or nonexistent body.
- `RegionKey`: body + deterministic latitude/longitude grid index + generator revision; longitude wraps and region IDs do not depend on visit order.
- `RegionalSurvey`: candidate surface conditions vs committed material stocks; include season/observation timestamp/confidence. Sensor results may be incomplete, not forged absolute facts.
- `WorldGenesisLedger`: finite initial rock/water/soil/volatiles/biomass stocks, with any generated source allocations debited from genesis reserves and journaled in SQLite. Persistent collection/mining cannot be driven from regenerable noise alone.
- A generation upgrade may add derived data but must not rewrite already surveyed/settled regions without an explicit migration with recoverable before/after snapshots.

## 5. Performance
Global profile generated once per planet. Region climate/relief cache by body/revision and 36×72 initial logical cells; higher-resolution tiles on exploration. Save source facts and deltas, not hundreds of millions of transient objects. Far-off colonies use coarse ticks; visited sites keep fully authoritative inventories, named NPCs and effect receipts. No dependency on full-fluid or N-body simulations.

## 6. Required acceptance (not yet passed)
1. Same identity + generator version yields bit-for-bit stable region facts across Windows/Linux, restarted process and visit order; different seeds can differ.
2. Longitude seam and poles do not create jumps or invalid negative mass; surface region neighborhoods vary continuously where a physical surface is continuous.
3. Temperate, arid, ice and ocean worlds can have region-specific environments and settlement tradeoffs, while sterile rock can remain ecologically uniform—**never assert N mandatory biomes**.
4. Arid world tests: high/low deserts, dry prairies, realistic source-driven rivers and temporarily dry arroyos, sheltered canyons and possible illuminated caverns; no unearned river water or vegetation biomass.
5. Frozen world test: warmest tropical candidate ≤0°C with poles below −51°C when configured, allowed seasonal surface melts/ocean openings when energy budget permits, and valid worlds where no liquid-water opening occurs; ecological potential is not proof of life.
6. Conservation: generation and player extraction allocate from one world ledger, preserve total matter, reject double allocations and replayed commands, recover from injected SQLite failures.
7. Gameplay: scanning produces local prospects; actual physical arrival and authorized equipment determine whether construction, farming or resource use can occur.

## 7. Rollout gates
- **PGF-01A (this PR):** versioned C++ adapter/specification and contract tests; do not claim it is a mature climate simulation.
- **PGF-01B:** coherent elevation and connected watershed/erosion graph, rainfall shadow and ocean shelves.
- **PGF-01C:** season and regional energy/water balance, meltwater and ice/ocean openings; fixed-point cross-platform determinism.
- **PGF-01D:** independent biosphere generation, microhabitats and persistent ecological material stocks.
- **PGF-01E:** `PersistentWorld` snapshot version/migration, cache invalidation, world genesis accounting.
- **PGF-01F:** planetary landing, settlement construction, farming, cargo and return-waste host authorizations and player-visible surveying.
